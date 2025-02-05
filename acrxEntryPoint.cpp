// (C) Copyright 2002-2012 by Autodesk, Inc. 

#include "StdAfx.h"
#include "resource.h"
#include <tchar.h>
#include <vector>
#include <list>
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

	static void ADSKTest_TEST_INVERTER_BLOCK() {
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

		ads_point pt1, pt2;
		pt1[0] = extents.minPoint().x;
		pt1[1] = extents.minPoint().y;
		pt1[2] = extents.minPoint().z;
		pt2[0] = extents.maxPoint().x;
		pt2[1] = extents.maxPoint().y;
		pt2[2] = extents.maxPoint().z;

		resbuf* filter = acutBuildList(RTDXF0, _T("INSERT"), NULL);
		ads_name ss;
		int result = acedSSGet(L"C", pt2, pt1, filter, ss);
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

					if (blockName.find(L"MMS Table") != -1 || blockName.find(L"MMS") != -1) {
						// Add block reference to the list
						blockList.push_back(pBlockRef.object());
					}
				}
			}
		}

		// Draw circles around each block reference in the blockList
		if (pDb->getBlockTable(pBlockTable, AcDb::kForRead) != Acad::eOk) return;
		AcDbBlockTableRecord* pBlockTableRecord = nullptr;
		if (pBlockTable->getAt(ACDB_MODEL_SPACE, pBlockTableRecord, AcDb::kForWrite) != Acad::eOk) {
			pBlockTable->close();
			return;
		}
		pBlockTable->close();

		for (auto pBlockRef : blockList) {
			DrawCircleAroundInverterBlock(pBlockRef, pBlockTableRecord);
		}

		pBlockTableRecord->close();

		// Clean up
		if (filter != nullptr) {
			acutRelRb(filter);
		}
		acedSSFree(ss);
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
ACED_ARXCOMMAND_ENTRY_AUTO(CArxProjectSampleApp, ADSKTest, _TEST_INVERTER_BLOCK, _TEST_INVERTER_BLOCK, ACRX_CMD_MODAL, NULL)
