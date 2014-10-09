#include "pch.h"
#include "../ogre/common/Def_Str.h"
#include "../ogre/common/RenderConst.h"
#include "../ogre/common/ShapeData.h"
#include "../vdrift/dbl.h"
#include "Road.h"
#ifndef SR_EDITOR
	#include "../vdrift/game.h"
#else
	#include "../editor/CApp.h"
	#include "../bullet/BulletCollision/CollisionShapes/btTriangleMesh.h"
	#include "../bullet/BulletCollision/CollisionShapes/btBvhTriangleMeshShape.h"
	#include "../bullet/BulletCollision/CollisionDispatch/btCollisionObject.h"
	#include "../bullet/BulletDynamics/Dynamics/btDiscreteDynamicsWorld.h"
#endif
#include <OgreTimer.h>
#include <OgreTerrain.h>
#include <OgreMeshManager.h>
#include <OgreEntity.h>
using namespace Ogre;
using std::vector;  using std::min;  using std::max;

#ifdef SR_EDITOR
#define LogR(a)  //LogO(String("~ Road  ") + a);
#else
#define LogR(a)
#endif


//  2-1	 6-5
//  | 0--7 |
//  3______4  front wall indices
const static int WFid[6][3] = {{2,1,0},{3,2,0},{5,4,7},{6,5,7}, {7,3,0},{4,3,7}};

struct stWiPntW {  Real x,y, uv, nx,ny;  };  // wall width points
const static int ciwW = 7;  // wall  width steps - types..
const static stWiPntW wiPntW[ciwW+1][2] = {  // section shape
	//  normal road                     //  pipe wall
	{{-0.5f, -0.0f, 0.0f,  1.0f, 0.0f}, {-0.28f, 0.68f,0.0f, -1.0f, 0.0f}},
	{{-0.5f,  1.2f, 0.5f,  0.5f, 0.5f}, {-0.28f, 0.5f, 0.2f, -0.5f, 0.5f}},
	{{-0.56f, 1.2f, 0.2f, -0.5f, 0.5f}, {-0.28f, 0.0f, 0.2f, -0.5f, 0.0f}},
	{{-0.56f,-0.9f, 1.6f, -0.5f,-0.5f}, {-0.2f, -0.9f, 0.5f, -0.1f,-0.5f}},
	{{ 0.56f,-0.9f, 3.0f,  0.5f,-0.5f}, { 0.2f, -0.9f, 0.5f,  0.1f,-0.5f}},
	{{ 0.56f, 1.2f, 1.6f,  0.5f, 0.5f}, { 0.28f, 0.0f, 0.2f,  0.5f, 0.0f}},
	{{ 0.5f,  1.2f, 0.2f, -0.5f, 0.5f}, { 0.28f, 0.5f, 0.2f,  0.5f, 0.5f}},
	{{ 0.5f, -0.0f, 0.5f, -1.0f, 0.0f}, { 0.28f, 0.68f,0.2f,  1.0f, 0.0f}}};


///  Rebuild geometry
//------------------------------------------------------------------------------

void SplineRoad::RebuildRoadInt(bool editorAlign, bool bulletFull)
{
	using std::vector;  using std::min;  using std::max;

	if (!rebuild && !(editorAlign || bulletFull))  return;
	rebuild = false;

	UpdRot(); //


	DataRoad DR(editorAlign,bulletFull);
	
	PrepassRange(DR);
	
	
	//  full rebuild
	Ogre::Timer ti;	
	
	if (iDirtyId == -1)
	{
		DestroyRoad();
		for (int seg=0; seg < DR.segs; ++seg)
		{
			RoadSeg rs;  rs.empty = true;
			vSegs.push_back(rs);
		}
	}


	//  Auto angles
	PrepassAngles(DR);


	///  LOD
	///------------------------------------------

	DataLod0 DL0;

	for (int lod = 0; lod < LODs; ++lod)
	{
		LogR("LOD: "+toStr(lod)+" ---");

		DataLod DL;
		StatsLod ST;

		PrepassLod(DR,DL0,DL,ST, lod, editorAlign);
		

		///  segment
		//--------------------------------------------------------------------------------------------------------------------------

		//>  mesh data  W-wall  C-column  B-blend
		vector<Vector4> clr0/*empty*/, clr, clrB;
		vector<Vector3> pos,norm, posW,normW, posC,normC, posLod, posB,normB;
		vector<Vector2> tcs, tcsW, tcsC, tcsB;
		Real tc1 = 0;  DL.tcLen = 0;
		int iLmrg = 0, iLmrgW = 0, iLmrgC = 0, iLmrgB = 0;
		Vector3 vlOld;

		int sNum = DR.sMax - DR.sMin, segM = DR.sMin;//, sNumO = sNum;
		while (sNum > 0)
		{
			int seg = (segM + DR.segs) % DR.segs;  // iterator
			int seg1 = getNext(seg), seg0 = getPrev(seg);
			
			//if (isLod0)
			//LogR("[Seg]  cur: " + toStr(seg) + "/" + toStr(sNumO) + "  all:" + toStr(segs));/**/

			//  on terrain  (whole seg)
			bool onTer = mP[seg].onTer && mP[seg1].onTer;
			
			// on merging segs only for game in whole road rebuild
			// off for editor (partial, 4segs rebuild)
			bool bNew = true, bNxt = true;

			if (bMerge)
			{
				bNew = (segM == DR.sMin/*1st*/)	|| DL.vbSegMrg[seg];
				bNxt = (segM+1 == DR.sMax/*last*/) || DL.vbSegMrg[seg1];  // next is new
			}
			
			if (bNew)  //> new seg data
			{	iLmrg = 0;	iLmrgW = 0;  iLmrgC = 0;  iLmrgB = 0;

				pos.clear();  norm.clear();  tcs.clear();  clr.clear();
				posW.clear(); normW.clear(); tcsW.clear();
				posC.clear(); normC.clear(); tcsC.clear();
				posB.clear(); normB.clear(); tcsB.clear(); clrB.clear();
			}

			//  bullet create
			bool blt = true;  // game always
			#ifdef SR_EDITOR  // editor only sel segs for align ter tool, or full for objects sim
				blt = bulletFull || editorAlign && (vSel.find(seg) != vSel.end());
			#endif
			

			///  destroy old
			RoadSeg& rs = vSegs[seg];
			if (!rs.empty && DL.isLod0)
				DestroySeg(seg);


			//  material
			int mid = mP[seg].idMtr, mtrId = max(0,mid);
			bool pipe = isPipe(seg);
			rs.sMtrRd = pipe ? sMtrPipe[mtrId] : (sMtrRoad[mtrId] + (onTer ? "_ter" :""));

			/// >  blend 2 materials
			bool hasBlend = false;
			if (mid != mP[seg1].idMtr && !pipe && !isPipe(seg1))
			{
				hasBlend = true;
				int mtrB = max(0,mP[seg1].idMtr);
				rs.sMtrB = sMtrRoad[mtrB] + (onTer ? "_ter" :"");
			}
			
			//  skirt /\ not for bridges
			bool useSkirt = onTer || pipe;  // pipe own factor..
			Real skLen = useSkirt ? skirtLen : 0.f, skH = useSkirt ? skirtH : 0.f;
			
			
			//  seg params  -----------------
			const int iwC = colN;  // column  polygon steps
						
			//  steps len
			int il = DL.viL[seg];
			int il0= DL0.viLSteps0[seg];
			Real la = 1.f / il;
			Real la0= 1.f / il0 * skLen;
			Real l = -la0;

			//  width
			//Real wi1 = fabs(mP[seg].width), wi2 = fabs(mP[seg1].width), wi12 = wi2-wi1;

			///  angles ()__
			Real ay1 = mP[seg].aYaw, ay2 = mP[seg1].aYaw, ay21 = ay2-ay1;
			Real ar1 = mP[seg].aRoll,ar2 = mP[seg1].aRoll,ar21 = ar2-ar1;
			const Real asw = 180;	// more than 180 swirl - wrong at start/end
			while (ay21 > asw)  ay21 -= 2*asw;  while (ay21 <-asw)  ay21 += 2*asw;
			while (ar21 > asw)  ar21 -= 2*asw;	while (ar21 <-asw)  ar21 += 2*asw;

			//  tc begin,range
			Real tcBeg = (seg > 0) ? DL.vSegTc[seg-1]       : 0.f,  tcEnd  = DL.vSegTc[seg],        tcRng  = tcEnd - tcBeg;
			Real tcBeg0= (seg > 0) ? DL0.vSegTc0[seg-1]: 0.f,  tcEnd0 = DL0.vSegTc0[seg], tcRng0 = tcEnd0 - tcBeg0;
			Real tcRmul = tcRng0 / tcRng;
			

			//------------------------------------------------------------------------------------
			//  Length  vertices
			//------------------------------------------------------------------------------------
			//LogR( " __len");
			if (mP[seg].idMtr >= 0)  // -1 hides segment
			for (int i = -1; i <= il+1; ++i)  // length +1  +2-gap
			{
				++iLmrg;
				///  length <dir>  |
				Vector3 vL0 = interpolate(seg, l);
				Vector3 vl = GetLenDir(seg, l, l+la), vw;
				Real len = vl.length();  vl.normalise();
				
				//  len tc
				if (i <= 0)  DL.tcLen = 0;
				Real tc = DL.tcLen * tcRmul + tcBeg0;
				//  skirt tc
				if (i == -1)	tc =-skLen* tcRmul + tcBeg0;
				if (i == il+1)  tc = skLen* tcRmul + tcEnd0;
				
				///  width <dir>   --
				if (mP[seg].onTer && mP[seg1].onTer)
				{	vw = Vector3(vl.z, 0, -vl.x);  }
				else		/// angles ()__
				{	Real ay = ay1 + ay21 * l;  // linear-
					Real ar = ar1 + ar21 * l;
					//Real ay = interpAYaw(seg,l);  // spline~
					//Real ar = interpARoll(seg,l);  // err swirl..
					vw = GetRot(ay,ar);  // from angles
				}
				vw.normalise();
				Vector3 vwn = vw;

				//Real wiMul = wi1 + wi12 * l;  // linear-
				Real wiMul = interpWidth(seg, l);  // spline~
				if (editorAlign)  // wider road for align terrain tool
					wiMul = wiMul*edWmul + edWadd;
				vw *= wiMul;

				//  on terrain ~~
				bool onTer1 = onTer || mP[seg].onTer && i==0 || mP[seg1].onTer && i==il;

				///  normal <dir>  /
				Vector3 vn = vl.crossProduct(vw);  vn.normalise();
				if (i==0)	vn = DL0.vnSeg0[seg];  // seg start=end
				if (i==il)	vn = DL0.vnSeg0[seg1];
				//Vector3 vnu = vn;  if (vnu.y < 0)  vnu = -vnu;  // always up y+


				//  width steps <->
				//int iw = viW[seg];
				int iw = DL.viwLS[seg][i+1];  //i = -1 .. il+1

				//  pipe width
				Real l01 = max(0.f, min(1.f, Real(i)/Real(il) ));
				Real p1 = mP[seg].pipe, p2 = mP[seg1].pipe;
				Real fPipe = p1 + (p2-p1)*l01;
				bool trans = (p1 == 0.f || p2 == 0.f) && !DL.viwEq[seg];
				Real trp = (p1 == 0.f) ? 1.f - l01 : l01;
				//LogR("   il="+toStr(i)+"/"+toStr(il)+"   iw="+toStr(iw)
				//	/*+(bNew?"  New ":"") +(bNxt?"  Nxt ":"")/**/);
				if (hasBlend)
					++iLmrgB;
				
				///  road ~    Width  vertices
				//-----------------------------------------------------------------------------------------------------------------
				Vector3 vH0, vH1;  //#  positions for bank angle
				int w0 = pipe ? iw/4   : 0,
					w1 = pipe ? iw*3/4 : iw;

				Real tcL = tc * (pipe ? tcMulP : tcMul);
				for (int w=0; w <= iw; ++w)  // width +1
				{
					//  pos create
					Vector3 vP,vN;	Real tcw = Real(w)/Real(iw);

					Real yTer = 0.f;
					if (fPipe == 0.f)
					{	//  flat --
						vP = vL0 + vw * (tcw - 0.5);
						vN = vn;
						yTer = mTerrain ? mTerrain->getHeightAtWorldPosition(vP.x, 0, vP.z) : 0.f;
						if (onTer1)  //  onTerrain
						{
							vP.y = yTer + fHeight * ((w==0 || w==iw) ? 0.15f : 1.f);
							vN = mTerrain ? TerUtil::GetNormalAt(mTerrain,
								vP.x, vP.z, DL.fLenDim*0.5f /*0.5f*/) : Vector3::UNIT_Y;
						}
					}else
					{	///  pipe (_)
						Real oo = (tcw - 0.5)/0.5 * PI_d * fPipe, so = sinf(oo), co = cosf(oo);
						vP = vL0 + vw  * 0.5 * so +
								 + vn * (0.5 - 0.5 * co) * wiMul;
						vN = vn * co + vwn * so;
						//LogO(toStr(w)+" "+fToStr(so,2,4));

						if (vN.y < 0.f)  vN.y = -vN.y;
						if (trans)  //  transition from flat to pipe
						{	vP += vw * (tcw - 0.5) * trp;  }
						yTer = mTerrain ? mTerrain->getHeightAtWorldPosition(vP.x, 0, vP.z) : 0.f;
					}
					
					//  skirt, gap patch_
					if (i == -1 || i == il+1)
						vP -= vn * skH;


					//  color - for minimap preview
					//  ---~~~====~~~---
					Real brdg = min(1.f, std::abs(vP.y - yTer) * 0.4f);  //par ] height diff mul
					Real h = max(0.f, 1.f - std::abs(vP.y - yTer) / 30.f);  // for grass dens tex
					Vector4 c(brdg,fPipe, 1.f, h);
					Vector2 vtc(tcw * 1.f /**2p..*/, tcL);

					//>  data road
					pos.push_back(vP);   norm.push_back(vN);
					tcs.push_back(vtc);  clr.push_back(c);
					if (hasBlend)
					{	// alpha, transition
						c.z = std::max(0.f, std::min(1.f, float(i)/il ));  //rand()%1000/1000.f;
						posB.push_back(vP);   normB.push_back(vN);
						tcsB.push_back(vtc);  clrB.push_back(c);
					}					
					//#  stats
					if (vP.y < ST.stMinH)  ST.stMinH = vP.y;
					if (vP.y > ST.stMaxH)  ST.stMaxH = vP.y;
					if (w==w0)  vH0 = vP;  //#
					if (w==w1)  vH1 = vP;
				}
				//#  stats  banking angle
				if (DL.isLod0 && i==0)
				{
					float h = (vH0.y - vH1.y), w = vH0.distance(vH1), d = fabs(h/w), a = asin(d)*180.f/PI_d;
					ST.bankAvg += a;
					if (a > ST.bankMax)  ST.bankMax = a;
					//LogO("RD seg :" + toStr(seg)+ "  h " + fToStr(h,1,3)
					//	+ "  w " + fToStr(w,1,3)+ "  d " + fToStr(d,1,3)+ "  a " + fToStr(a,1,3) );
				}
				

				///  wall ]
				//------------------------------------------------------------------------------------
				Real uv = 0.f;  // tc long

				if (!onTer)
				if (i >= 0 && i <= il)  // length +1
				{	++iLmrgW;
					Real tcLW = tc * (pipe ? tcMulPW : tcMulW);
					for (int w=0; w <= ciwW; ++w)  // width +1
					{
						int pp = (p1 > 0.f || p2 > 0.f) ? 1 : 0;  //  pipe wall
						stWiPntW wP = wiPntW[w][pp];

						if (trans /*&& (w <= 3 || w >= iwW-3)*/)
						{	wP.x *= 1 + trp;  wP.y *= 1 - trp;  }
						uv += wP.uv;

						Vector3 vP = vL0 + vw * wP.x + vn * wP.y;
						Vector3 vN =     vwn * wP.nx + vn * wP.ny;  vN.normalise();

						//>  data Wall
						posW.push_back(vP);  normW.push_back(vN);
						tcsW.push_back(0.25f * Vector2(uv, tcLW));  //par
					}
				}
				
				
				///  columns |
				//------------------------------------------------------------------------------------
				if (!onTer && mP[seg].cols > 0)
				if (i == il/2)  // middle-
				{	++iLmrgC;
					const Real r = colR;  // column radius

					for (int h=0; h <= 1; ++h)  // height
					for (int w=0; w <= iwC; ++w)  // width +1
					{
						Real a = Real(w)/iwC *2*PI_d,  //+PI_d/4.f
							x = r*cosf(a), y = r*sinf(a);

						Vector3 vlXZ(vl.x, 0.f, vl.z);  Real fl = 1.f/max(0.01f, vlXZ.length());
						Vector3 vP = vL0 + fl * vl * x + vwn * y;
						Real yy;

						if (h==0)  // top below road
						{	yy = vn.y * -0.8f;  //par
							vP.y += yy;
						}
						else  // bottom below ground
						{	yy = (mTerrain ? mTerrain->getHeightAtWorldPosition(vP) : 0.f) - 0.3f;
							vP.y = yy;
						}

						Vector3 vN(vP.x-vL0.x, 0.f, vP.z-vL0.z);  vN.normalise();

						//>  data Col
						posC.push_back(vP);  normC.push_back(vN);
						tcsC.push_back(Vector2( Real(w)/iwC * 4, vP.y * tcMulC ));  //par
					}
				}
				
				
				if (i == -1 || i == il)  // add len
				{	l += la0;  DL.tcLen += len;  }
				else
				{	l += la;  DL.tcLen += len;  }
			}
			//  Length  vertices
			//------------------------------------------------------------------------------------
			

			//  lod vis points
			if (DL.isLod0)
			{	int lps = max(2, (int)(DL.vSegLen[seg] / lposLen));

				for (int p=0; p <= lps; ++p)
				{
					Vector3 vp = interpolate(seg, Real(p)/Real(lps));
					posLod.push_back(vp);
				}
			}


			//---------------------------------------------------------------------------------------------------------
			///  create mesh  indices
			//---------------------------------------------------------------------------------------------------------
			blendTri = false;
			if (bNxt && !pos.empty())  /*Merging*/
			{
				String sEnd = toStr(idStr);  ++idStr;
				String sMesh = "rd.mesh." + sEnd, sMeshW = sMesh + "W", sMeshC = sMesh + "C", sMeshB = sMesh + "B";

				posBt.clear();
				idx.clear();  // set for addTri
				idxB.clear();
				at_pos = &pos;  at_size = pos.size();  at_ilBt = iLmrg-2;
				bltTri = blt;  blendTri = hasBlend;
				
				///  road ~
				int iiw = 0;  //LogR( " __idx");

				//  equal width steps
				if (DL.viwEq[seg]==1)
					for (int i = 0; i < iLmrg-1; ++i)  // length-1 +2gap
					{
						int iw = DL.viW[seg];  // grid  w-1 x l-1 x2 tris
						for (int w=0; w < iw; ++w)  // width-1
						{
							//LogR( "   il="+toStr(i)+"/"+toStr(il)+"   iw="+toStr(iw));
							int f0 = iiw + w, f1 = f0 + (iw+1);
							addTri(f0+0,f1+1,f0+1,i);
							addTri(f0+0,f1+0,f1+1,i);
						}
						iiw += iw+1;
					}
				else
					//  pipe, diff width_
					for (int i = 0; i < iLmrg-1; ++i)  // length-1 +2gap
					{
						int iw = DL.viwLS[seg][i], iw1 = DL.viwLS[seg][i+1];
						int sw = iw1 < iw ? 1 : 0;
						//LogR( "   il="+toStr(i)+"/"+toStr(il)+"   iw="+toStr(iw));
						
						//int w=0;  // test fans
						for (int w=0; w < iw -sw; ++w)  // width-1
						{
							int f0 = iiw + w, f1 = f0 + (iw+1);
							//  |\ |  f0+0  f0+1
							//  | \|  f1+0  f1+1
							if (sw==0) {
								addTri(f0+0,f1+1,f0+1,i);
								addTri(f0+0,f1+0,f1+1,i);  }
							else {  // |/|
								addTri(f0+0,f1+0,f0+1,i);
								addTri(f0+1,f1+0,f1+1,i);  }
						}

						///>>>  fix gaps when iw changes - fan tris
						int ma = iw1 - iw, ms = -ma, m;
						for (m=0; m < ma; ++m)
						{
							int f0 = iiw + iw-1, f1 = f0 + (iw+2)+m;
							addTri(f0+1,f1+0,f1+1,i);
						}
						for (m=0; m < ms; ++m)
						{
							int f0 = iiw + iw-sw -m, f1 = f0 + (iw+1);
							addTri(f0+0,f1+0,f0+1,i);
						}
						iiw += iw + 1;
					}
				vSegs[seg].nTri[lod] = idx.size()/3;
				blendTri = false;


				//  create Ogre Mesh
				//-----------------------------------------
				MeshPtr meshOld = MeshManager::getSingleton().getByName(sMesh);
				if (!meshOld.isNull())  LogR("Mesh exists !!!" + sMesh);

				AxisAlignedBox aabox;
				MeshPtr mesh = MeshManager::getSingleton().createManual(sMesh,"General");
				SubMesh* sm = mesh->createSubMesh();
				
				CreateMesh(sm, aabox, pos,norm,clr,tcs, idx, rs.sMtrRd);

				MeshPtr meshW, meshC, meshB;  // ] | >
				bool wall = !posW.empty();
				if (wall)
				{
					meshW = MeshManager::getSingleton().createManual(sMeshW,"General");
					meshW->createSubMesh();
				}
				bool cols = !posC.empty() && DL.isLod0;  // cols have no lods
				if (cols)
				{
					meshC = MeshManager::getSingleton().createManual(sMeshC,"General");
					meshC->createSubMesh();
				}
				if (hasBlend)
				{
					meshB = MeshManager::getSingleton().createManual(sMeshB,"General");
					sm = meshB->createSubMesh();
					CreateMesh(sm, aabox, posB,normB,clrB,tcsB, idxB, rs.sMtrB);
				}
				//*=*/wall = 0;  cols = 0;  // test


				///  wall ]
				//------------------------------------------------------------------------------------
				bool jfw0 = !mP[seg].onTer  && mP[seg0].idMtr < 0;  // jump front wall, ends in air
				bool jfw1 = !mP[seg1].onTer && mP[seg1].idMtr < 0;
				bool pipeGlass = pipe && bMtrPipeGlass[ mP[seg].idMtr ];  // pipe glass mtr
				if (wall)
				{
					idx.clear();
					for (int i = 0; i < iLmrgW-1; ++i)  // length
					{	int iiW = i* (ciwW+1);

						for (int w=0; w < ciwW; ++w)  // width
						{
							int f0 = iiW + w, f1 = f0 + (ciwW+1);
							idx.push_back(f0+1);  idx.push_back(f1+1);  idx.push_back(f0+0);
							idx.push_back(f1+1);  idx.push_back(f1+0);  idx.push_back(f0+0);
						}
					}
					
					//  front plates start,end  |_|  not in pipes
					int i,f, b = posW.size()-ciwW-1;
					if (!pipe)
					{
						int ff = jfw0 ? 6 : 4;
						for (f=0; f < ff; ++f)
							for (i=0; i<=2; ++i)  idx.push_back( WFid[f][i] );
						
						ff = jfw1 ? 6 : 4;
						for (f=0; f < ff; ++f)
							for (i=0; i<=2; ++i)  idx.push_back( WFid[f][2-i]+b );
	
						vSegs[seg].nTri[lod] += idx.size()/3;
					}
					
					sm = meshW->getSubMesh(0);   // for glass only..
					rs.sMtrWall = !pipeGlass ? sMtrWall : sMtrWallPipe;
					if (!posW.empty())
						CreateMesh(sm, aabox, posW,normW,clr0,tcsW, idx, rs.sMtrWall);
				}
				
				
				///  columns |
				//------------------------------------------------------------------------------------
				if (cols)
				{
					idx.clear();
					at_pos = &posC;

					for (int l=0; l < iLmrgC; ++l)
					for (int w=0; w < iwC; ++w)
					{
						int f0 = w + l*(iwC+1)*2, f1 = f0 + iwC+1;
						addTri(f0+0, f1+1, f0+1, 1);
						addTri(f0+0, f1+0, f1+1, 1);
					}					
					vSegs[seg].nTri[lod] += idx.size()/3;

					sm = meshC->getSubMesh(0);
					//if (!posC.empty())
					CreateMesh(sm, aabox, posC,normC,clr0,tcsC, idx, sMtrCol);
				}
				
								
				//  add Mesh to Scene  -----------------------------------------
				Entity* ent = 0, *entW = 0, *entC = 0, *entB = 0;
				SceneNode* node = 0, *nodeW = 0, *nodeC = 0, *nodeB = 0;

				AddMesh(mesh, sMesh, aabox, &ent, &node, "."+sEnd);
				if (pipeGlass)
				{
					//ent->setCastShadows(true);
					ent->setRenderQueueGroup(RQG_PipeGlass);
				}else
					ent->setRenderQueueGroup(RQG_Road);

				if (wall /*&& !posW.empty()*/)
				{
					AddMesh(meshW, sMeshW, aabox, &entW, &nodeW, "W."+sEnd);
					entW->setCastShadows(true);  // only cast
				}
				if (cols /*&& !posC.empty()*/)
				{
					AddMesh(meshC, sMeshC, aabox, &entC, &nodeC, "C."+sEnd);
					entC->setVisible(true);
					if (bCastShadow)
						entC->setCastShadows(true);
				}
				if (hasBlend)
				{
					AddMesh(meshB, sMeshB, aabox, &entB, &nodeB, "B."+sEnd);
					entB->setRenderQueueGroup(RQG_RoadBlend);
				}

				if (bCastShadow && !onTer)
					ent->setCastShadows(true);

				
				//>>  store ogre data  ------------
				rs.road[lod].node = node;	rs.wall[lod].node = nodeW;	 rs.blend[lod].node = nodeB;
				rs.road[lod].ent = ent;		rs.wall[lod].ent = entW;	 rs.blend[lod].ent = entB;
				rs.road[lod].mesh = mesh;	rs.wall[lod].mesh = meshW;	 rs.blend[lod].mesh = meshB;
				rs.road[lod].smesh = sMesh; rs.wall[lod].smesh = sMeshW; rs.blend[lod].smesh = sMeshB;
				if (DL.isLod0)  {
					rs.col.node = nodeC;
					rs.col.ent = entC;
					rs.col.mesh = meshC;
					rs.col.smesh = sMeshC;  }
				rs.empty = false;  // new

				//  copy lod points
				if (DL.isLod0)
				{	for (size_t p=0; p < posLod.size(); ++p)
						rs.lpos.push_back(posLod[p]);
					posLod.clear();
				}
				//#  stats--
				if (ST.stats)
				{
					rs.mrgLod = (iMrgSegs % 2)*2+1;  //-
					iMrgSegs++;	 // count, full
				}


				///  bullet trimesh  at lod 0
				///------------------------------------------------------------------------------------
				if (DL.isLod0 && blt)
				{
					btTriangleMesh* trimesh = new btTriangleMesh();  vbtTriMesh.push_back(trimesh);
					#define vToBlt(v)  btVector3(v.x, -v.z, v.y)
					#define addTriB(a,b,c)  trimesh->addTriangle(vToBlt(a), vToBlt(b), vToBlt(c))

					size_t si = posBt.size(), a=0;  // %3!
					for (size_t i=0; i < si/3; ++i,a+=3)
						addTriB(posBt[a], posBt[a+1], posBt[a+2]);

					// if (cols)  // add columns^..
					
					//  Road  ~
					btCollisionShape* shape = new btBvhTriangleMeshShape(trimesh, true);
					size_t su = (pipe ? SU_Pipe : SU_Road) + mtrId;
					shape->setUserPointer((void*)su);  // mark as road/pipe + mtrId
					shape->setMargin(0.01f);  //?
					
					btCollisionObject* bco = new btCollisionObject();
					btTransform tr;  tr.setIdentity();  //tr.setOrigin(pc);
					bco->setActivationState(DISABLE_SIMULATION);
					bco->setCollisionShape(shape);	bco->setWorldTransform(tr);
					bco->setFriction(0.8f);   //+
					bco->setRestitution(0.f);
					bco->setCollisionFlags(bco->getCollisionFlags() |
						btCollisionObject::CF_STATIC_OBJECT | btCollisionObject::CF_DISABLE_VISUALIZE_OBJECT/**/);
					#ifdef SR_EDITOR
						pApp->world->addCollisionObject(bco);
						bco->setUserPointer((void*)111);  // mark road
					#else
						pGame->collision.world->addCollisionObject(bco);
						pGame->collision.shapes.push_back(shape);
					#endif
					
					//  Wall  ]
					#ifndef SR_EDITOR  // in Game
					if (wall)
					{	trimesh = new btTriangleMesh();  vbtTriMesh.push_back(trimesh);
						
						for (int i = 0; i < iLmrgW-1; ++i)  // length
						{	int iiW = i* (ciwW+1);

							for (int w=0; w < ciwW; ++w)  // width
							if (bRoadWFullCol || w==0 || w == ciwW-1)  // only 2 sides|_| optym+
							{
								int f0 = iiW + w, f1 = f0 + (ciwW+1);
								addTriB(posW[f0+0], posW[f1+1], posW[f0+1]);
								addTriB(posW[f0+0], posW[f1+0], posW[f1+1]);
							}
						}
						//  front plates start,end  |_|
						int f, b = posW.size()-ciwW-1;
						if (!pipe)
						{
							int ff = jfw0 ? 6 : 4;
							for (f=0; f < ff; ++f)
								addTriB(posW[WFid[f][0]], posW[WFid[f][1]], posW[WFid[f][2]]);

							ff = jfw1 ? 6 : 4;
							for (f=0; f < ff; ++f)
								addTriB(posW[WFid[f][2]+b], posW[WFid[f][1]+b], posW[WFid[f][0]+b]);
						}
						
						btCollisionShape* shape = new btBvhTriangleMeshShape(trimesh, true);
						shape->setUserPointer((void*)SU_RoadWall);  //wall and column same object..
						
						btCollisionObject* bco = new btCollisionObject();
						bco->setActivationState(DISABLE_SIMULATION);
						bco->setCollisionShape(shape);	bco->setWorldTransform(tr);
						bco->setFriction(0.1f);   //+
						bco->setRestitution(0.f);
						bco->setCollisionFlags(bco->getCollisionFlags() |
							btCollisionObject::CF_STATIC_OBJECT | btCollisionObject::CF_DISABLE_VISUALIZE_OBJECT/**/);
						pGame->collision.world->addCollisionObject(bco);
						pGame->collision.shapes.push_back(shape);
					}
					#endif
				}
	
			}/*bNxt Merging*/

			sNum--;  segM++;  // next
		}
		///  segment end
		//--------------------------------------------------------------------------------------------------------------------------
	
		//#  stats
		if (ST.stats)
		{
			st.HeightDiff = max(0.f, ST.stMaxH - ST.stMinH);
			st.bankAvg = ST.bankAvg / DR.segs;
			st.bankMax = ST.bankMax;
			//LogO("RD bank angle:  avg "+fToStr(st.bankAvg,1,3)+"  max "+fToStr(st.bankMax,1,3));
		}
	}
	//  lod end

	
	UpdLodVis(fLodBias);
	if (iDirtyId == -1)
		iOldHide = -1;


	if (iDirtyId == -1)
	//if (segs <= 4 || sMax - sMin > 4)
		LogO(String("::: Time Road Rebuild: ") + fToStr(ti.getMilliseconds(),0,3) + " ms");
}


//  add triangle
void SplineRoad::addTri(int f1, int f2, int f3, int i)
{
	/*bool ok = true;  const int fmax = 65530; //16bit
	if (f1 >= at_size || f1 > fmax)  {  LogRE("idx too big: "+toStr(f1)+" >= "+toStr(at_size));  ok = 0;  }
	if (f2 >= at_size || f2 > fmax)  {  LogRE("idx too big: "+toStr(f2)+" >= "+toStr(at_size));  ok = 0;  }
	if (f3 >= at_size || f3 > fmax)  {  LogRE("idx too big: "+toStr(f3)+" >= "+toStr(at_size));  ok = 0;  }
	if (!ok)  return;/**/

	idx.push_back(f1);  idx.push_back(f2);  idx.push_back(f3);
	if (blendTri)
	{
		idxB.push_back(f1);  idxB.push_back(f2);  idxB.push_back(f3);
	}

	if (bltTri && i > 0 && i < at_ilBt)
	{
		posBt.push_back((*at_pos)[f1]);
		posBt.push_back((*at_pos)[f2]);
		posBt.push_back((*at_pos)[f3]);
	}
}


//---------------------------------------------------------------------------------------
void SplineRoad::PrepassRange(DataRoad& DR)
{
	//  segments range
	DR.segs = getNumPoints();
	if (DR.segs == 0 || DR.segs == 1)  return;

	
	if (vSegs.size() != DR.segs || DR.editorAlign || DR.bulletFull)
		iDirtyId = -1;  // force full
		
	DR.sMin = 0;  DR.sMax = DR.segs;
	//  update 4 segs only (fast)
	if (iDirtyId != -1 && DR.segs >= 4)
	{
		DR.sMin = iDirtyId-2;
		DR.sMax = iDirtyId+2;
		if (!isLooped)
			DR.sMin = std::max(0, DR.sMin);
	}
	if (!isLooped)  // 1 seg less
		DR.sMax = std::min(DR.segs-1, DR.sMax);
}

///  Auto angles prepass ...
//---------------------------------------------------------------------------------------
void SplineRoad::PrepassAngles(DataRoad& DR)
{
	if (DR.segs > 2)
	for (int seg=0; seg < DR.segs; ++seg)
	{
		int seg0 = getPrev(seg);
				
		if (mP[seg].aType == AT_Manual)
		{	mP[seg].aYaw = mP[seg].mYaw;  mP[seg].aRoll = mP[seg].mRoll;  }
		else
		{	mP[seg].aRoll = 0.f;
			/// ... roll getangle?, +180 loops?, len
			const Real dist = 0.1f;
			Vector3 vl = GetLenDir(seg, 0.f, dist) + GetLenDir(seg0, 1.f-dist, 1.f);  //vl.normalise();
			Vector3 vw = Vector3(vl.z, 0.f, -vl.x);  //vw.normalise();
			mP[seg].aYaw = TerUtil::GetAngle(vw.x, vw.z) *180.f/PI_d;

			if (mP[seg].aType == AT_Both)
			{	mP[seg].aYaw += mP[seg].mYaw;  mP[seg].aRoll += mP[seg].mRoll;  }	
		}
	}
}

///  Prepass LOD,  data for segments
//---------------------------------------------------------------------------------------
const int ciLodDivs[LODs] = {1,2,4,8};

void SplineRoad::PrepassLod(
	const DataRoad& DR,
	DataLod0& DL0, DataLod& DL, StatsLod& ST,
	int lod, bool editorAlign)
{
	int iLodDiv = ciLodDivs[lod];
	DL.isLod0 = lod == 0;
	DL.fLenDim = fLenDim0 * iLodDiv;
		
	//if (isLod0)?
	LogR("--- Lod segs prepass ---");
	for (int seg=0; seg < DR.segs; ++seg)
	{
		int seg1 = getNext(seg), seg0 = getPrev(seg);

		//  width steps  --
		Real sp = mP[seg].pipe, sp1 = mP[seg1].pipe, sp0 = mP[seg0].pipe;
		Real p = sp * iwPmul, pl = max(sp, sp1)* iwPmul/4;
		if (p < 0.f)  p = 1.f;  else  p = 1.f + p;
		if (pl< 0.f)  pl= 1.f;  else  pl= 1.f + pl;
		bool pipe = sp > 0.f || sp1 > 0.f;
		int wmin = pipe ? 5 : 1;  // min w steps  //par

		int iw = max(1/*wmin*/, (int)(p * iWidthDiv0 / iLodDiv));  //* wid/widDiv..
		DL.viW.push_back(iw);
		int iwl = max(1, (int)(pl * iWidthDiv0 / iLodDiv));

		//  length steps  |
		Real len = GetSegLen(seg);
		int  il = int(len / DL.fLenDim) / iwl * iwl + iwl;
		Real lenAdd = 1.f / il;
		
		DL.viL.push_back(il);
		DL.vSegLen.push_back(len);

		ST.roadLen += len;  //#
		if (pipe)
		{	ST.rdPipe += len; //#
			if (mP[seg].onPipe)  ST.rdOnPipe += len;  //#
		}

		///-  Merge conditions
		DL.sumLenMrg += len;
		//  mtr changes
		int hid = mP[seg].idMtr, hid1 = mP[seg1].idMtr, hid0 = mP[seg0].idMtr;
		LogR(toStr(sp0) + "  " + toStr(sp) + "  " + toStr(sp1));

		//  merge road and pipe segs, don't merge transitions
		if (sp != sp1 || sp != sp0  ||  hid != hid1 || hid != hid0)
		{	DL.sumLenMrg = 0.f;  ++DL.mrgCnt;
			DL.vbSegMrg.push_back(1);
		}
		else  //  onTer change
		if (mP[seg].onTer != mP[seg1].onTer || mP[seg].onTer != mP[seg0].onTer)
		{	DL.sumLenMrg = 0.f;  ++DL.mrgCnt;
			DL.vbSegMrg.push_back(1);
		}
		else  if (DL.sumLenMrg >= setMrgLen)
		{	DL.sumLenMrg -= setMrgLen;  ++DL.mrgCnt;
			DL.vbSegMrg.push_back(1);  // bNew
		}else
			DL.vbSegMrg.push_back(0);  // merged
		
		LogR("seg "+toStr(seg)+"  iw "+toStr(iw)+"  il "+toStr(il)+"  pp "+toStr(sp));
		
		if (DL.isLod0)
			DL0.viLSteps0.push_back(il);


		///  length <dir>  |
		Vector3 vl = GetLenDir(seg, 0, lenAdd), vw;  vl.normalise();
		Real ay = mP[seg].aYaw, ar = mP[seg].aRoll;
		
		///  width <dir>   ---
		if (mP[seg].onTer && mP[seg1].onTer)  //  perpendicular on xz
		{	vw = Vector3(vl.z, 0, -vl.x);  vw.normalise(); 
			//mP[seg].angle = atan2(vl.z, -vl.x)*180.f/PI_d+90.f;  // set yaw..
		}else
			vw = GetRot(ay,ar);  // from angles
			
		///  normal <dir>  /
		if (DL.isLod0)
		{	Vector3 vn = vl.crossProduct(vw);  vn.normalise();
			//if (vn.y < 0.f)  vn = -vn;  // always up y+
			DL0.vnSeg0.push_back(vn);
		}

		{
		Real wiMul = mP[seg].width;
		if (editorAlign)  // wider road for align terrain tool
			wiMul = wiMul*edWmul + edWadd;
		vw *= wiMul;
		DL.vwSeg.push_back(vw);
		}

		ST.avgWidth += mP[seg].width * len;  //#
		if (!mP[seg].onTer || !mP[seg1].onTer)
			ST.rdOnT += len;  //#


		//  tc  seg il* len
		Real l = 0.f;
		for (int i = 0; i < il; ++i)  // length +1
		{
			//  length dir
			Vector3 vl = GetLenDir(seg, l, l+lenAdd);
			l += lenAdd;  DL.tcLen += vl.length();
		}
		DL.vSegTc.push_back(DL.tcLen);
		if (DL.isLod0)
			DL0.vSegTc0.push_back(DL.tcLen);
	}

	
	LogR("--- seg prepass2  viwLS  ---");
	for (int seg=0; seg < DR.segs; ++seg)
	{
		int seg1 = getNext(seg);
		int il = DL.viL[seg];
		std::vector<int> viwL;

		//  width steps per lenght point in cur seg
		int iw0 = DL.viW[seg], iw1 = DL.viW[seg1];
		//String ss="";
		for (int i = -1; i <= il+1; ++i)  // length +1  +2-gap
		{
			int ii = max(0, min(il, i));
			int iw = iw0 + (int)( Real(ii)/Real(il) * (iw1-iw0) );
			//if (i==0 || i == il)
			//	ss += toStr(iw)+" ";
			viwL.push_back(iw);
		}
		int eq = iw1==iw0 ? 1 : 0;

		DL.viwEq.push_back(eq);
		DL.viwLS.push_back(viwL);
		//if (!eq)  vbSegMrg[seg] = 1;
		//LogR("seg "+toStr(seg)+"  >> "+ss);
	}


	//#  stats  at lod0, whole road
	ST.stats = DL.isLod0 && iDirtyId == -1;
	if (ST.stats)
	{	st.Length = ST.roadLen;  st.WidthAvg = ST.avgWidth / ST.roadLen;
		st.OnTer = ST.rdOnT / ST.roadLen * 100.f;
		st.Pipes = ST.rdPipe / ST.roadLen * 100.f;
		st.OnPipe = ST.rdOnPipe / ST.roadLen * 100.f;
		segsMrg = DL.mrgCnt;
	}
}
