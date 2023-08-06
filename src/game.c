#include "main.h"
#include "loader.h"

#include <proto/exec.h>
#include <proto/graphics.h>
#include <proto/intuition.h>
#include <proto/dos.h>

#include <intuition/intuition.h>

#define MARGIN (DOT_SIZE / 2 + 1)
#define DOT_RADIUS (DOT_SIZE / 2)

#define ForEachFwd(l, t, v) for (v = (t*)(l)->mlh_Head; v->Node.mln_Succ; v = (t*)v->Node.mln_Succ)
#define ForEachRev(l, t, v) for (v = (t*)(l)->mlh_TailPred; v->Node.mln_Pred; v = (t*)v->Node.mln_Pred)

/*---------------------------------------------------------------------------*/
/* Game levels are generated in virtual coordinate system: a square of 32768 */
/* x 32768 units, zero in upper left corner. At start and after every window */
/* resize dot coordinates are transformed to the game area in window         */
/* RastPort. Transformation does not mainatin aspect ratio, game is squeezed */
/* as needed to fit the game area.                                           */
/*                                                                           */
/* When a dot is dragged, everything is done in RastPort coordinates. When   */
/* dragging is finished, virtual coordinates of the dot are updated with     */
/* inverse transformation. This is the only place where division is needed.  */
/*                                                                           */
/* Level completion test is done on virtual coordinates.                     */
/*---------------------------------------------------------------------------*/

void TransformAllDots(struct App *app)
{
	struct GameDot *gd;
	WORD fieldw = app->Field.MaxX - app->Field.MinX + 1;
	WORD fieldh = app->Field.MaxY - app->Field.MinY + 1;

	ForEachFwd(&app->DotList, struct GameDot, gd)
	{
		gd->Pixel.x = (mul16(gd->Virtual.x, fieldw) >> 15) + app->Field.MinX;
		gd->Pixel.y = (mul16(gd->Virtual.y, fieldh) >> 15) + app->Field.MinY;
	}
}

/*---------------------------------------------------------------------------*/

void DrawLines(struct RastPort *rp, struct MinList *lines)
{
	struct GameLine *gl;
	
	ForEachFwd(lines, struct GameLine, gl)
	{
		Move(rp, gl->StartDot->Pixel.x, gl->StartDot->Pixel.y);
		Draw(rp, gl->EndDot->Pixel.x, gl->EndDot->Pixel.y);
	}
}

/*---------------------------------------------------------------------------*/

void DrawDots(struct RastPort *rp, struct App *app)
{
	WORD dotradius = DOT_SIZE >> 1;
	struct GameDot *gd;
	
	ForEachFwd(&app->DotList, struct GameDot, gd)
	{
		BltMaskBitMapRastPort(app->DotBitMap, 0, 0, rp, gd->Pixel.x - dotradius,
			gd->Pixel.y - dotradius, DOT_SIZE, DOT_SIZE, 0xE0, (APTR)app->DotRaster);
	}
}

/*---------------------------------------------------------------------------*/

void DrawGame(struct App *app)
{
	struct RastPort *rp = app->Win->RPort;
	SetAPen(rp, 1);
	SetDrMd(rp, JAM1);
	DrawLines(rp, &app->LineList);
	DrawDots(rp, app);	
}

/*---------------------------------------------------------------------------*/

void ScaleGame(struct App *app)
{
	app->Field.MinX = app->Win->BorderLeft + MARGIN;
	app->Field.MinY = app->Win->BorderTop + MARGIN;
	app->Field.MaxX = app->Win->Width - app->Win->BorderRight - MARGIN - 1;
	app->Field.MaxY = app->Win->Height - app->Win->BorderBottom - MARGIN - 1;
	TransformAllDots(app);
}

/*---------------------------------------------------------------------------*/

void EraseGame(struct App *app)
{
	struct RastPort *rp = app->Win->RPort;
	SetAPen(rp, 0);
	SetDrMd(rp, JAM1);
	RectFill(rp, app->Win->BorderLeft, app->Win->BorderTop, app->Win->Width -
	 app->Win->BorderRight - 1, app->Win->Height - app->Win->BorderBottom - 1);
}

/*---------------------------------------------------------------------------*/

static inline BOOL MaskHit(UWORD x, UWORD y, CONST UWORD *mask)
{
	return (mask[y] & (0x8000 >> x));
}

/*---------------------------------------------------------------------------*/

static inline BOOL InsideBox(UWORD clkx, UWORD clky, UWORD dotx, UWORD doty)
{
	if (clkx < dotx - DOT_RADIUS) return FALSE;
	if (clkx > dotx + DOT_RADIUS) return FALSE;
	if (clky < doty - DOT_RADIUS) return FALSE;
	if (clky > doty + DOT_RADIUS) return FALSE;
	return TRUE;
}

/*---------------------------------------------------------------------------*/

BOOL DotClicked(struct App *app, struct GameDot* dot, UWORD x, UWORD y)
{
	if (InsideBox(dot->Pixel.x, dot->Pixel.y, x, y))
	{
		UWORD bbx = x + DOT_RADIUS - dot->Pixel.x;
		UWORD bby = y + DOT_RADIUS - dot->Pixel.y;
		if (MaskHit(bbx, bby, app->DotRaster)) return TRUE;
	}
	
	return FALSE;
}

/*---------------------------------------------------------------------------*/

struct GameDot* FindClickedDot(struct App *app, UWORD x, UWORD y)
{
	struct GameDot *gd;
	
	ForEachRev(&app->DotList, struct GameDot, gd)
	{
		if (DotClicked(app, gd, x, y)) return gd;
	}
	
	return NULL; 
}

/*---------------------------------------------------------------------------*/

static inline void MoveDraggedDot(struct App *app, struct GameDot *clicked)
{
	Remove((struct Node*)clicked);
	app->DraggedDot = clicked;
}

/*---------------------------------------------------------------------------*/

static inline void MoveDraggedLines(struct App *app, struct GameDot *clicked)
{
	struct GameLine *gl, *gl2;
	
	gl = (struct GameLine*)app->LineList.mlh_Head;
	
	while (gl->Node.mln_Succ)
	{
		gl2 = (struct GameLine*)gl->Node.mln_Succ;
		
		if ((gl->StartDot == clicked) || (gl->EndDot == clicked))
		{
			Remove((struct Node*)gl);
			AddTail((struct List*)&app->DraggedLines, (struct Node*)gl);
		}

		gl = gl2;	
	}
}

/*---------------------------------------------------------------------------*/

void MoveDraggedItems(struct App *app, struct GameDot *clicked)
{
	MoveDraggedDot(app, clicked);
	MoveDraggedLines(app, clicked);
}

/*---------------------------------------------------------------------------*/

static inline void MoveDraggedDotBack(struct App *app)
{
	AddTail((struct List*)&app->DotList, (struct Node*)app->DraggedDot);
	app->DraggedDot = NULL;
}

/*---------------------------------------------------------------------------*/

static inline void MoveDraggedLinesBack(struct App *app)
{
	struct GameLine *gl;

	while (gl = (struct GameLine*)RemHead((struct List*)&app->DraggedLines))
	{
		AddTail((struct List*)&app->LineList, (struct Node*)gl);
	}
}

/*---------------------------------------------------------------------------*/

void MoveDraggedItemsBack(struct App *app)
{
	MoveDraggedDotBack(app);
	MoveDraggedLinesBack(app);
}

/*---------------------------------------------------------------------------*/

void EraseDot(struct App *app, struct GameDot *gd)
{
	BltTemplate(app->DotRaster, 0, 2, app->Win->RPort, gd->Pixel.x - DOT_RADIUS,
	 gd->Pixel.y - DOT_RADIUS, DOT_SIZE, DOT_SIZE);
}

/*---------------------------------------------------------------------------*/

void DrawDraggedItems(struct App *app)
{
	struct RastPort *rp = app->Win->RPort;
	
	SetDrMd(rp, JAM1 | COMPLEMENT);
	SetAPen(rp, 1);
	DrawLines(rp, &app->DraggedLines);
	EraseDot(app, app->DraggedDot);
}

/*---------------------------------------------------------------------------*/

void EraseDraggedItems(struct App *app)
{
	struct RastPort *rp = app->Win->RPort;

	SetAPen(rp, 0);
	EraseDot(app, app->DraggedDot);
	DrawLines(rp, &app->DraggedLines);
}

/*---------------------------------------------------------------------------*/

void GameClick(struct App *app, WORD x, WORD y)
{
	struct GameDot *clicked;

	if (clicked = FindClickedDot(app, x, y))
	{ 	
		MoveDraggedItems(app, clicked);
		EraseDraggedItems(app);
		DrawGame(app);
		DrawDraggedItems(app);
		app->Win->Flags |= WFLG_REPORTMOUSE;
	}
}

/*---------------------------------------------------------------------------*/

static inline void UpdateDragPosition(struct App *app, WORD x, WORD y)
{
	if (x < app->Field.MinX) x = app->Field.MinX;
	if (x > app->Field.MaxX) x = app->Field.MaxX;
	if (y < app->Field.MinY) y = app->Field.MinY;
	if (y > app->Field.MaxY) y = app->Field.MaxY;
	app->DraggedDot->Pixel.x = x;
	app->DraggedDot->Pixel.y = y;
}

/*---------------------------------------------------------------------------*/

void GameUnclick(struct App *app, WORD x, WORD y)
{
	app->Win->Flags &= ~WFLG_REPORTMOUSE;
	DrawDraggedItems(app);
	UpdateDragPosition(app, x, y);
//	InverseTransformDot(app->DraggedDot);
	MoveDraggedItemsBack(app);
	DrawGame(app);
}

/*---------------------------------------------------------------------------*/

void GameDotDrag(struct App *app, WORD x, WORD y)
{
	DrawDraggedItems(app);
	UpdateDragPosition(app, x, y);	
	DrawDraggedItems(app);	
}

/*---------------------------------------------------------------------------*/

void NewGame(struct App *app)
{
	LoadLevel();
}

/*---------------------------------------------------------------------------*/

void DisposeGame(struct App *app)
{
	if (app->LineStorage) FreeVec(app->LineStorage);
	if (app->DotStorage) FreeVec(app->DotStorage);
	app->LineStorage = NULL;
	app->DotStorage = NULL;
	InitList(&app->DotList);
	InitList(&app->LineList);
	InitList(&app->DraggedLines);
	app->DraggedDot = NULL;
}
