// (C) Copyright 2002-2012 by Autodesk, Inc. 

#include "StdAfx.h"
#include "resource.h"
#include <tchar.h>
#include <vector>
#include <list>
#include <chrono>
#include <array> 
#include <acutads.h>
#include "aced.h"        // AutoCAD command functions
#include "acdocman.h"    // Document management
#include "dbents.h"      // Entity classes
#include "actrans.h"     // Transactions
#include "geassign.h"    // Geometric conversions
#include <Shlobj.h> // Include for SHGetFolderPathW
#include "acedads.h" 
#include "acedCmdNF.h"
#pragma comment(lib, "Shell32.lib") // Link against Shell32.lib
#define szRDS _RXST("MK")

class CArxProjectSampleApp : public AcRxArxApp {
public:
	CArxProjectSampleApp() : AcRxArxApp() {}

	virtual AcRx::AppRetCode On_kInitAppMsg(void* pkt) {
		return AcRxArxApp::On_kInitAppMsg(pkt);
	}

	virtual AcRx::AppRetCode On_kUnloadAppMsg(void* pkt) {
		return AcRxArxApp::On_kUnloadAppMsg(pkt);
	}

	virtual void RegisterServerComponents() {}

	static void ADSKTest_TEST_DRAW_LINE() {
		AcGePoint3d startPt, endPt;
		if (acedGetPoint(NULL, L"\nSpecify start point: ", asDblArray(startPt)) != RTNORM) return;
		if (acedGetPoint(NULL, L"\nSpecify end point: ", asDblArray(endPt)) != RTNORM) return;

		AcDbLine* pLine = new AcDbLine(startPt, endPt);
		AcDbBlockTable* pBlockTable;
		AcDbDatabase* pDb = acdbHostApplicationServices()->workingDatabase();

		if (pDb->getBlockTable(pBlockTable, AcDb::kForRead) != Acad::eOk) return;

		AcDbBlockTableRecord* pBlockTableRecord;
		if (pBlockTable->getAt(ACDB_MODEL_SPACE, pBlockTableRecord, AcDb::kForWrite) != Acad::eOk) {
			pBlockTable->close();
			return;
		}

		pBlockTable->close();
		pBlockTableRecord->appendAcDbEntity(pLine);
		pBlockTableRecord->close();
		pLine->close();
	}

	static AcDbPolyline* SelectBlockBoundary() {
		ads_name entName;
		ads_point pt;
		int result = acedEntSel(L"Select Block Boundary (Polyline):", entName, pt);

		if (result != RTNORM) {
			acutPrintf(L"Error: No boundary polyline selected.\n");
			return nullptr;
		}

		AcDbObjectId objId;
		if (acdbGetObjectId(objId, entName) != Acad::eOk) {
			acutPrintf(L"Error: Failed to get object ID.\n");
			return nullptr;
		}

		AcDbEntity* pEntity = nullptr;
		if (acdbOpenObject(pEntity, objId, AcDb::kForRead) != Acad::eOk) {
			acutPrintf(L"Error: Failed to open entity.\n");
			return nullptr;
		}

		if (!pEntity->isKindOf(AcDbPolyline::desc())) {
			acutPrintf(L"Error: Selected entity is not a polyline.\n");
			pEntity->close();
			return nullptr;
		}

		// Proceed with the polyline entity
		AcDbPolyline* pPolyline = AcDbPolyline::cast(pEntity);
		return pPolyline;
	}

	static void CreateBoundaryPointsAndFilter(AcDbPolyline* boundaryPolyline, AcGePoint3dArray& boundaryPoints) {
		// Create a Point3dArray to store the polyline's vertices.
		for (int i = 0; i < static_cast<int>(boundaryPolyline->numVerts()); i++) {
			AcGePoint3d pt;
			boundaryPolyline->getPointAt(i, pt);
			boundaryPoints.append(pt);
		}
	}

	static resbuf* SelectCrossingPolygon(AcDbPolyline* pPolyline) {
		int numberOFVertices = static_cast<int>(pPolyline->numVerts());
		if (numberOFVertices < 2) {
			acutPrintf(L"Error: Polyline must have at least 2 vertices.\n");
			return nullptr;
		}
		struct resbuf* ptsRb = nullptr;
		struct resbuf* lastRb = nullptr;
		for (int i = 0; i < numberOFVertices; i++) {
			AcGePoint3d pt;
			pPolyline->getPointAt(i, pt);

			struct resbuf* rb = acutNewRb(RT3DPOINT);
			rb->resval.rpoint[0] = pt.x;
			rb->resval.rpoint[1] = pt.y;
			rb->resval.rpoint[2] = pt.z;  // Use Z = 0 if 2D polyline

			if (ptsRb == nullptr) {
				ptsRb = rb;
			}
			else {
				lastRb->rbnext = rb;
			}
			lastRb = rb;
		}

		// Ensure the polygon is closed by adding the first point at the end
		if (ptsRb != nullptr) {
			struct resbuf* rb = acutNewRb(RT3DPOINT);
			rb->resval.rpoint[0] = ptsRb->resval.rpoint[0];
			rb->resval.rpoint[1] = ptsRb->resval.rpoint[1];
			rb->resval.rpoint[2] = ptsRb->resval.rpoint[2];
			lastRb->rbnext = rb;
		}
		return ptsRb;
	}

	static void ADSKTest_Test_TABLE_PILING() {
		const wchar_t* filename = L"CoordMark.dwg";
		const wchar_t* blockName = L"AUTOCRDS";
		try {
			// Get the path to the source DWG file
			wchar_t tempPath[MAX_PATH];
			SHGetFolderPathW(NULL, CSIDL_APPDATA, NULL, 0, tempPath);
			std::wstring dwgPath = std::wstring(tempPath) + L"\\Autodesk\\ApplicationPlugins\\SolarInfraCADAutomate.bundle\\Resources\\" + filename;

			// Open the source DWG file
			AcDbDatabase* pOpenDb = new AcDbDatabase(Adesk::kFalse);
			if (pOpenDb->readDwgFile(dwgPath.c_str()) != Acad::eOk) {
				acutPrintf(L"\nError: Unable to read DWG file.");
				delete pOpenDb;
				return;
			}

			// Collect block definitions from source DWG file
			AcDbObjectIdArray ids;
			AcDbBlockTable* pBlockTable;
			if (pOpenDb->getBlockTable(pBlockTable, AcDb::kForRead) != Acad::eOk) {
				acutPrintf(L"\nError: Unable to open block table.");
				delete pOpenDb;
				return;
			}

			AcDbObjectId blockId;
			if (pBlockTable->getAt(blockName, blockId) == Acad::eOk) {
				ids.append(blockId);
			}
			pBlockTable->close();

			if (ids.length() != 0) {
				AcDbDatabase* pDestDb = acdbHostApplicationServices()->workingDatabase();

				// Clone the new block from the source database
				AcDbIdMapping idMap;
				if (pDestDb->wblockCloneObjects(ids, pDestDb->blockTableId(), idMap, AcDb::kDrcReplace) != Acad::eOk) {
					acutPrintf(L"\nError: Unable to clone block definitions.");
				}
			}

			delete pOpenDb;
		}
		catch (const Acad::ErrorStatus& es) {
			acutPrintf(L"\nAutoCAD Error: %d", es);
		}
		catch (const std::exception& ex) {
			acutPrintf(L"\nException: %s", ex.what());
		}
	}

	static void ADSKTest_TEST_INVERTER_BLOCK() {
		auto start = std::chrono::high_resolution_clock::now();
		AcDbDatabase* pDb = acdbHostApplicationServices()->workingDatabase();
		AcDbBlockTable* pBlockTable = nullptr;
		AcDbPolyline* pPolyline = SelectBlockBoundary();
		if (pPolyline == nullptr) {
			return;
		}

		AcDbExtents extents;
		if (pPolyline->getGeomExtents(extents) != Acad::eOk) {
			acutPrintf(L"Error: Failed to get geometric extents.\n");
			pPolyline->close();
			return;
		}
		int numberOFVertices = static_cast<int>(pPolyline->numVerts());
		if (numberOFVertices < 2) {
			acutPrintf(L"Error: Polyline must have at least 2 vertices.\n");
			return;
		}

		// Create the resbuf linked list for the crossing polygon points
		struct resbuf* ptsRb = SelectCrossingPolygon(pPolyline);

		resbuf* filter = acutBuildList(RTDXF0, _T("INSERT"), NULL);
		if (filter == nullptr) {
			acutPrintf(L"Error: Failed to build filter list.\n");
			acutRelRb(ptsRb);  // Clean up the resbuf linked list
			return;
		}

		ads_name ss;
		int result = acedSSGet(L"CP", ptsRb, NULL, filter, ss);

		acutPrintf(L"\nacedSSGet result: %d", result);

		std::vector<AcDbBlockReference*> blockList;
		if (result == RTNORM) {
			Adesk::Int32 length;
			acedSSLength(ss, &length);
			for (Adesk::Int32 i = 0; i < length; i++) {
				ads_name ent;
				acedSSName(ss, i, ent);
				AcDbObjectId objId;
				acdbGetObjectId(objId, ent);

				AcDbObjectPointer<AcDbBlockReference> pBlockRef(objId, AcDb::kForRead);
				if (pBlockRef.openStatus() != Acad::eOk) continue;
				AcDbObjectId blockTableRecordId = pBlockRef->blockTableRecord();
				AcDbBlockTableRecord* pBlockTableRecord = nullptr;
				if (acdbOpenObject(pBlockTableRecord, blockTableRecordId, AcDb::kForRead) == Acad::eOk) {
					AcString blockName;
					pBlockTableRecord->getName(blockName);
					pBlockTableRecord->close();
					AcGePoint3d centerPoint = GetTableCenter(pBlockRef.object());
					if ((blockName.find(L"MMS Table") != -1 || blockName.find(L"MMS") != -1) && IsPointInsidePolyline(pPolyline, centerPoint)) {
						// Add block reference to the list
						blockList.push_back(pBlockRef.object());
					}
				}
			}
		}
		// Perform the equivalent LINQ operation in C++
		std::map<int, std::vector<AcDbBlockReference*>> groupedBlocks;
		for (auto pBlockRef : blockList) {
			AcDbExtents extents;
			if (pBlockRef->getGeomExtents(extents) == Acad::eOk) {
				double y = extents.maxPoint().y;
				double x = extents.maxPoint().x;
				int groupKey = static_cast<int>(y / 0.005);
				groupedBlocks[groupKey].push_back(pBlockRef);
			}
		}

		std::vector<AcDbBlockReference*> filteredBlockList;
		for (auto& group : groupedBlocks) {
			auto& blockRefs = group.second;
			auto it = std::max_element(blockRefs.begin(), blockRefs.end(), [](AcDbBlockReference* a, AcDbBlockReference* b) {
				AcDbExtents extentsA, extentsB;
				a->getGeomExtents(extentsA);
				b->getGeomExtents(extentsB);
				return extentsA.maxPoint().x > extentsB.maxPoint().x;
				});
			if (it != blockRefs.end()) {
				filteredBlockList.push_back(*it);
			}
		}
		std::sort(filteredBlockList.begin(), filteredBlockList.end(), [](AcDbBlockReference* blockRef1, AcDbBlockReference* blockRef2) {
			if (blockRef1 != nullptr && blockRef2 != nullptr) {
				AcDbExtents extents1, extents2;
				blockRef1->getGeomExtents(extents1);
				blockRef2->getGeomExtents(extents2);
				return extents1.maxPoint().y > extents2.maxPoint().y;
			}
			return false; // Default comparison result if any object is null
			});

		// Add a layer in layer table with name "$GRID" if it does not exist
		AcDbLayerTable* pLayerTable;
		if (pDb->getLayerTable(pLayerTable, AcDb::kForRead) != Acad::eOk) return;
		if (!pLayerTable->has(L"$GRID")) {
			pLayerTable->upgradeOpen();
			AcDbLayerTableRecord* pLayerRecord = new AcDbLayerTableRecord();
			pLayerRecord->setName(L"$GRID");
			pLayerTable->add(pLayerRecord);
			pLayerRecord->close();
			pLayerTable->close();
		}
		if (pDb->getBlockTable(pBlockTable, AcDb::kForRead) != Acad::eOk) return;
		AcDbBlockTableRecord* pBlockTableRecord;
		if (pBlockTable->getAt(ACDB_MODEL_SPACE, pBlockTableRecord, AcDb::kForWrite) != Acad::eOk) {
			pBlockTable->close();
			return;
		}
		pBlockTable->close();
		double leaderLength = 15.0;
		if (!filteredBlockList.empty()) {
			acutPrintf(L"Found %d block references with one corner inside the boundary region.\n", filteredBlockList.size());
			int count = 1;

			for (auto pBlockRef : filteredBlockList) {
				AcDbExtents extents;
				if (pBlockRef->getGeomExtents(extents) != Acad::eOk) continue;

				AcGePoint3d cornerPointMax = extents.maxPoint();
				AcGePoint3d cornerPointMin = extents.minPoint();
				double widthCenter = (cornerPointMax.y + cornerPointMin.y) / 2;



				AcGePoint3d leaderStart(cornerPointMin.x - leaderLength, widthCenter, 0);
				AcGePoint3d leaderEnd(cornerPointMin.x, widthCenter, 0);
				AcGePoint3d centerPoint(leaderStart.x - (cornerPointMax.y - cornerPointMin.y) * 0.7, leaderStart.y, 0);

				// Create circle around the text
				AcDbCircle* pCircle = new AcDbCircle(centerPoint, AcGeVector3d::kZAxis, (cornerPointMax.y - cornerPointMin.y) * 0.7);
				pCircle->setColorIndex(1); // Set circle color
				pCircle->setLayer(L"$GRID");
				pCircle->setLinetype(L"CONTINUOUS");
				pBlockTableRecord->appendAcDbEntity(pCircle);
				pCircle->close();

				AcDbTextStyleTable* pTextStyleTable;
				if (pDb->getTextStyleTable(pTextStyleTable, AcDb::kForRead) != Acad::eOk) return;
				AcDbObjectId textStyleId;
				AcDbTextStyleTableRecord* pTextStyleRecord;
				if (pTextStyleTable->getAt(L"Arial", pTextStyleRecord, AcDb::kForRead) == Acad::eOk) {
					textStyleId = pTextStyleRecord->objectId();
					pTextStyleRecord->close();
				}
				else {
					pTextStyleTable->upgradeOpen();
					AcDbTextStyleTableRecord* pNewTextStyle = new AcDbTextStyleTableRecord();
					pNewTextStyle->setName(L"Arial");
					pNewTextStyle->setFileName(L"Arial.ttf"); // Assuming TrueType font, adjust if necessary
					if (pTextStyleTable->add(pNewTextStyle) == Acad::eOk) {
						textStyleId = pNewTextStyle->objectId();
						pNewTextStyle->close();
					}
				}
				pTextStyleTable->close();

				AcDbMText* pMText = new AcDbMText();
				pMText->setContents(std::to_wstring(count).c_str());
				pMText->setLocation(centerPoint);
				pMText->setAttachment(AcDbMText::kMiddleCenter);
				pMText->setTextHeight((cornerPointMax.y - cornerPointMin.y) * 0.6);
				pMText->setLayer(L"$GRID");
				pMText->setColorIndex(7);
				pMText->setTextStyle(textStyleId);
				pBlockTableRecord->appendAcDbEntity(pMText);
				pMText->close();

				AcDbLeader* pLeader = new AcDbLeader();
				pLeader->appendVertex(leaderStart);
				pLeader->appendVertex(leaderEnd);
				pLeader->setColorIndex(1);
				pLeader->setLinetype(L"CENTER");
				pLeader->setHasArrowHead(false);
				pLeader->setLayer(L"$GRID");
				pBlockTableRecord->appendAcDbEntity(pLeader);
				pLeader->close();

				count++;
			}
		}
		else {
			acutPrintf(L"No block references found inside the boundary region.\n");
		}
		acedCommandS(RTSTR, L"_.ATTDIA", RTSTR, L"0", RTNONE);
		pBlockTableRecord->close();
		for (int i = 0; i < numberOFVertices; i++) {
			AcGePoint3d pt;
			pPolyline->getPointAt(i, pt);
			// Use the command line to insert the block
			acedCommandS(RTSTR, L"-INSERT", RTSTR, L"AUTOCRDS", RTPOINT, AcGePoint2d(pt.x, pt.y), RTREAL, 1.0, RTREAL, 1.0, RTREAL, 0.0, RTSTR, "", RTSTR, "", RTNONE);
		}
		// Clean up
		if (filter != nullptr) {
			acutRelRb(filter);
		}
		acedSSFree(ss);
		// Stop the stopwatch
		auto end = std::chrono::high_resolution_clock::now();
		std::chrono::duration<double> elapsed = end - start;
		acutPrintf(L"Elapsed time: .\n", elapsed.count());
		/*AcString message;
		message.format(L"Elapsed time: %f seconds.", elapsed.count());
		acedAlert(message.kwszPtr());*/
	}

	static void InsertBlocksAtPolylineVertices(AcDbPolyline* pPolyline, AcDbDatabase* pDb, int numberOfVertices) {
		if (!pPolyline || !pDb) return;

		// Open the block table
		AcDbBlockTable* pBlockTable;
		if (pDb->getBlockTable(pBlockTable, AcDb::kForRead) != Acad::eOk) {
			acutPrintf(L"Error: Unable to open block table.\n");
			return;
		}

		// Get block definition (block table record)
		AcDbBlockTableRecord* pBlockDef;
		if (pBlockTable->getAt(L"AUTOCRDS", pBlockDef, AcDb::kForRead) != Acad::eOk) {
			pBlockTable->close();
			acutPrintf(L"Error: Block 'AUTOCRDS' not found!\n");
			return;
		}

		// Open model space for writing
		AcDbBlockTableRecord* pModelSpace;
		if (pBlockTable->getAt(ACDB_MODEL_SPACE, pModelSpace, AcDb::kForWrite) != Acad::eOk) {
			pBlockDef->close();
			pBlockTable->close();
			acutPrintf(L"Error: Unable to open model space.\n");
			return;
		}

		for (int i = 0; i < numberOfVertices; i++) {
			AcGePoint3d pt;
			pPolyline->getPointAt(i, pt);

			// Create block reference
			AcDbBlockReference* pBlockRef = new AcDbBlockReference(pt, pBlockDef->objectId());

			// Add to model space
			if (pModelSpace->appendAcDbEntity(pBlockRef) == Acad::eOk) {
				DrawCircleAroundInverterBlock(pBlockRef, pModelSpace);
				pBlockRef->close();
			}
			else {
				acutPrintf(L"Error: Unable to append block reference to model space.\n");
				delete pBlockRef; // Cleanup if failed
			}
		}

		// Close all opened objects
		pModelSpace->close();
		pBlockDef->close();
		pBlockTable->close();
	}


	static bool IsPointInsidePolyline(AcDbPolyline* polyline, const AcGePoint3d& point) {
		bool inside = false;

		if (polyline != nullptr) {
			int numVerts = polyline->numVerts();
			for (int i = 0, j = numVerts - 1; i < numVerts; j = i++) {
				AcGePoint2d start, end;
				polyline->getPointAt(i, start);
				polyline->getPointAt(j, end);

				// Apply tolerance to the Y comparison
				if ((start.y > point.y) != (end.y > point.y) &&
					point.x < (end.x - start.x) * (point.y - start.y) / (end.y - start.y) + start.x) {
					inside = !inside;
				}
			}
		}
		return inside;
	}
	static AcGePoint3d GetTableCenter(AcDbBlockReference* table) {
		AcDbExtents extents;
		if (table->getGeomExtents(extents) != Acad::eOk) {
			return AcGePoint3d::kOrigin; // Return origin if extents cannot be obtained
		}

		AcGePoint3d cornerPointMax = extents.maxPoint();
		AcGePoint3d cornerPointMin = extents.minPoint();
		return AcGePoint3d((cornerPointMax.x + cornerPointMin.x) / 2,
			(cornerPointMax.y + cornerPointMin.y) / 2,
			(cornerPointMax.z + cornerPointMin.z) / 2);
	}
	

	static void DrawCircleAroundInverterBlock(AcDbBlockReference* pBlockRef, AcDbBlockTableRecord* pBlockTableRecord) {
		AcGePoint3d center = GetTableCenter(pBlockRef);
		double radius = 2.0;
		AcDbCircle* pCircle = new AcDbCircle(center, AcGeVector3d::kZAxis, radius);
		pCircle->setColorIndex(1);
		pBlockTableRecord->appendAcDbEntity(pCircle);
		pCircle->close();
	}

	static void IterateOverModelSpace() {
		AcDbDatabase* pDb = acdbHostApplicationServices()->workingDatabase();
		AcDbBlockTable* pBlockTable = nullptr;
		AcDbPolyline* pPolyline = SelectBlockBoundary();
		if (pPolyline == nullptr) {
			return;
		}

		AcDbExtents extents;
		if (pPolyline->getGeomExtents(extents) != Acad::eOk) {
			acutPrintf(L"Error: Failed to get geometric extents.\n");
			pPolyline->close();
			return;
		}
		if (pDb->getBlockTable(pBlockTable, AcDb::kForRead) != Acad::eOk) return;

		AcDbBlockTableRecord* pBlockTableRecord = nullptr;
		if (pBlockTable->getAt(ACDB_MODEL_SPACE, pBlockTableRecord, AcDb::kForWrite) != Acad::eOk) {
			pBlockTable->close();
			return;
		}
		pBlockTable->close();

		AcDbBlockTableRecordIterator* pIterator = nullptr;
		if (pBlockTableRecord->newIterator(pIterator) != Acad::eOk) {
			pBlockTableRecord->close();
			return;
		}

		for (; !pIterator->done(); pIterator->step()) {
			AcDbEntity* pEntity = nullptr;
			if (pIterator->getEntity(pEntity, AcDb::kForRead) != Acad::eOk) continue;

			if (pEntity->isKindOf(AcDbBlockReference::desc())) {
				AcDbBlockReference* pBlockRef = AcDbBlockReference::cast(pEntity);
				AcDbObjectId blockId = pBlockRef->blockTableRecord();
				AcDbBlockTableRecord* pBlockDef = nullptr;

				if (acdbOpenObject(pBlockDef, blockId, AcDb::kForRead) == Acad::eOk) {
					AcString blockDefName;
					pBlockDef->getName(blockDefName);
					if (wcsstr(blockDefName.kwszPtr(), L"MMS Table") != nullptr) {
						// find the length and width of pBlockRef here using geometric extents
						AcDbExtents extents;
						if (pBlockRef->getGeomExtents(extents) == Acad::eOk) {
							double length = extents.maxPoint().x - extents.minPoint().x;
							double width = extents.maxPoint().y - extents.minPoint().y;

							if (length > width)
							{
								// Get the layer name of the block reference
								AcDbObjectId layerId = pBlockRef->layerId();
								AcDbLayerTableRecord* pLayerRecord = nullptr;

								if (acdbOpenObject(pLayerRecord, layerId, AcDb::kForRead) == Acad::eOk) {
									// Check if the layer is OFF
									AcGePoint3d tableCenter = GetTableCenter(pBlockRef);
									if (!pLayerRecord->isOff() && !pLayerRecord->isFrozen() && IsPointInsidePolyline(pPolyline, tableCenter)) {
										// Layer is ON, proceed with drawing the circle
										DrawCircleAroundInverterBlock(pBlockRef, pBlockTableRecord);
									}
									pLayerRecord->close();
								}
							}
						}
					}
					pBlockDef->close();
				}
			}
			pEntity->close();
		}

		delete pIterator;
		pBlockTableRecord->close();
	}
	static void MKMyGroupMyCommand() {
		acutPrintf(L"\nMyCommand executed.");
	}

	static void MKMyGroupMyPickFirst() {
		acutPrintf(L"\nMyPickFirst executed.");
	}
};

IMPLEMENT_ARX_ENTRYPOINT(CArxProjectSampleApp)

ACED_ARXCOMMAND_ENTRY_AUTO(CArxProjectSampleApp, ADSKTest, _TEST_DRAW_LINE, _TEST_DRAW_LINE, ACRX_CMD_MODAL, NULL)
ACED_ARXCOMMAND_ENTRY_AUTO(CArxProjectSampleApp, ADSKTest, _Test_TABLE_PILING, _Test_TABLE_PILING, ACRX_CMD_MODAL, NULL)
ACED_ARXCOMMAND_ENTRY_AUTO(CArxProjectSampleApp, ADSKTest, _TEST_INVERTER_BLOCK, _TEST_INVERTER_BLOCK, ACRX_CMD_MODAL, NULL)
