/* Copyright (C)2004 Landmark Graphics Corporation
 * Copyright (C)2005, 2006 Sun Microsystems, Inc.
 * Copyright (C)2009, 2011 D. R. Commander
 *
 * This library is free software and may be redistributed and/or modified under
 * the terms of the wxWindows Library License, Version 3.1 or (at your option)
 * any later version.  The full license is in the LICENSE.txt file included
 * with this distribution.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * wxWindows Library License for more details.
 */

#include <stdio.h>
#include <stdlib.h>
#include <sys/signal.h>
#include <string.h>
#include <math.h>
#include "rrtimer.h"
#include "rrthread.h"
#include "rrmutex.h"
#include "fakerconfig.h"
#define __FAKERHASH_STATICDEF__
#include "faker-winhash.h"
#include "faker-ctxhash.h"
#include "faker-vishash.h"
#include "faker-cfghash.h"
#include "faker-rcfghash.h"
#include "faker-pmhash.h"
#include "faker-glxdhash.h"
#include "faker-sym.h"
#include "glxvisual.h"
#define __VGLCONFIGSTART_STATICDEF__
#include "vglconfigstart.h"
#include <sys/types.h>
#include <unistd.h>

#ifdef SUNOGL
extern "C" {
static GLvoid r_glIndexd(OglContextPtr, GLdouble);
static GLvoid r_glIndexf(OglContextPtr, GLfloat);
static GLvoid r_glIndexi(OglContextPtr, GLint);
static GLvoid r_glIndexs(OglContextPtr, GLshort);
static GLvoid r_glIndexub(OglContextPtr, GLubyte);
static GLvoid r_glIndexdv(OglContextPtr, const GLdouble *);
static GLvoid r_glIndexfv(OglContextPtr, const GLfloat *);
static GLvoid r_glIndexiv(OglContextPtr, const GLint *);
static GLvoid r_glIndexsv(OglContextPtr, const GLshort *);
static GLvoid r_glIndexubv(OglContextPtr, const GLubyte *);
}
#endif

// Globals
Display *_localdpy=NULL;
#define _localdisplayiscurrent() (_glXGetCurrentDisplay()==_localdpy)
#define _isremote(dpy) (_localdpy && dpy!=_localdpy)
#define _isfront(drawbuf) (drawbuf==GL_FRONT || drawbuf==GL_FRONT_AND_BACK \
	|| drawbuf==GL_FRONT_LEFT || drawbuf==GL_FRONT_RIGHT || drawbuf==GL_LEFT \
	|| drawbuf==GL_RIGHT)
#define _isright(drawbuf) (drawbuf==GL_RIGHT || drawbuf==GL_FRONT_RIGHT \
	|| drawbuf==GL_BACK_RIGHT)

static inline int _drawingtofront(void)
{
	GLint drawbuf=GL_BACK;
	_glGetIntegerv(GL_DRAW_BUFFER, &drawbuf);
	return _isfront(drawbuf);
}

static inline int _drawingtoright(void)
{
	GLint drawbuf=GL_LEFT;
	_glGetIntegerv(GL_DRAW_BUFFER, &drawbuf);
	return _isright(drawbuf);
}

static rrcs globalmutex;
static int __shutdown=0;

static inline int isdead(void)
{
	int retval=0;
	globalmutex.lock(false);
	retval=__shutdown;
	globalmutex.unlock(false);
	return retval;
}

static void __vgl_cleanup(void)
{
	if(pmhash::isalloc()) pmh.killhash();
	if(vishash::isalloc()) vish.killhash();
	if(cfghash::isalloc()) cfgh.killhash();
	if(rcfghash::isalloc()) rcfgh.killhash();
	if(ctxhash::isalloc()) ctxh.killhash();
	if(glxdhash::isalloc()) glxdh.killhash();
	if(winhash::isalloc()) winh.killhash();
	__vgl_unloadsymbols();
}

void __vgl_safeexit(int retcode)
{
	int shutdown;
	globalmutex.lock(false);
	shutdown=__shutdown;
	if(!__shutdown)
	{
		__shutdown=1;
		__vgl_cleanup();
		fconfig_deleteinstance();
	}
	globalmutex.unlock(false);
	if(!shutdown) exit(retcode);
	else pthread_exit(0);
}

class _globalcleanup
{
	public:
		~_globalcleanup()
		{
			globalmutex.lock(false);
			fconfig_deleteinstance();
			__shutdown=1;
			globalmutex.unlock(false);
		}
};
_globalcleanup gdt;

#define _die(f,m) {if(!isdead())  \
	rrout.print("[VGL] ERROR: in %s--\n[VGL]    %s\n", f, m);  \
	__vgl_safeexit(1);}

#define TRY() try {
#define CATCH() } catch(rrerror &e) {_die(e.getMethod(), e.getMessage());}

static int __vgltracelevel=0;

#define prargd(a) rrout.print("%s=0x%.8lx(%s) ", #a, (unsigned long)a,  \
	a? DisplayString(a):"NULL")
#define prargs(a) rrout.print("%s=%s ", #a, a?a:"NULL")
#define prargx(a) rrout.print("%s=0x%.8lx ", #a, (unsigned long)a)
#define prargi(a) rrout.print("%s=%d ", #a, a)
#define prargf(a) rrout.print("%s=%f ", #a, (double)a)
#define prargv(a) rrout.print("%s=0x%.8lx(0x%.2lx) ", #a, (unsigned long)a,  \
	a? a->visualid:0)
#define prargc(a) rrout.print("%s=0x%.8lx(0x%.2x) ", #a, (unsigned long)a,  \
	a? _FBCID(a):0)
#define prargal11(a) if(a) {  \
	rrout.print(#a"=[");  \
	for(int __an=0; a[__an]!=None; __an++) {  \
		rrout.print("0x%.4x", a[__an]);  \
		if(a[__an]!=GLX_USE_GL && a[__an]!=GLX_DOUBLEBUFFER  \
			&& a[__an]!=GLX_STEREO && a[__an]!=GLX_RGBA)  \
			rrout.print("=0x%.4x", a[++__an]);  \
		rrout.print(" ");  \
	}  rrout.print("] ");}
#define prargal13(a) if(a) {  \
	rrout.print(#a"=[");  \
	for(int __an=0; a[__an]!=None; __an+=2) {  \
		rrout.print("0x%.4x=0x%.4x ", a[__an], a[__an+1]);  \
	}  rrout.print("] ");}

#define opentrace(f)  \
	double __vgltracetime=0.;  \
	if(fconfig.trace) {  \
		if(__vgltracelevel>0) {  \
			rrout.print("\n[VGL] ");  \
			for(int __i=0; __i<__vgltracelevel; __i++) rrout.print("  ");  \
		}  \
		else rrout.print("[VGL] ");  \
		__vgltracelevel++;  \
		rrout.print("%s (", #f);  \

#define starttrace()  \
		__vgltracetime=rrtime();  \
	}

#define stoptrace()  \
	if(fconfig.trace) {  \
		__vgltracetime=rrtime()-__vgltracetime;

#define closetrace()  \
		rrout.PRINT(") %f ms\n", __vgltracetime*1000.);  \
		__vgltracelevel--;  \
		if(__vgltracelevel>0) {  \
			rrout.print("[VGL] ");  \
			if(__vgltracelevel>1)  \
				for(int __i=0; __i<__vgltracelevel-1; __i++) rrout.print("  ");  \
    }  \
	}

#include "faker-glx.cpp"

// Used when VGL_TRAPX11=1
int xhandler(Display *dpy, XErrorEvent *xe)
{
	char temps[256];
	temps[0]=0;
	XGetErrorText(dpy, xe->error_code, temps, 255);
	rrout.PRINT("[VGL] WARNING: X11 error trapped\n[VGL]    Error:  %s\n[VGL]    XID:    0x%.8x\n",
		temps, xe->resourceid);
	return 0;
}

void __vgl_fakerinit(void)
{
	static int init=0;

	rrcs::safelock l(globalmutex);
	if(init) return;
	init=1;

	fconfig_reloadenv();
	if(strlen(fconfig.log)>0) rrout.logto(fconfig.log);

	if(fconfig.verbose)
		rrout.println("[VGL] %s v%s %d-bit (Build %s)",
			__APPNAME, __VERSION, (int)sizeof(size_t)*8, __BUILD);

	if(getenv("VGL_DEBUG"))
	{
		rrout.print("[VGL] Attach debugger to process %d ...\n", getpid());
		fgetc(stdin);
	}
	if(fconfig.trapx11) XSetErrorHandler(xhandler);

	__vgl_loadsymbols();
	if(!_localdpy)
	{
		if(fconfig.verbose) rrout.println("[VGL] Opening local display %s",
			strlen(fconfig.localdpystring)>0? fconfig.localdpystring:"(default)");
		if((_localdpy=_XOpenDisplay(fconfig.localdpystring))==NULL)
		{
			rrout.print("[VGL] ERROR: Could not open display %s.\n",
				fconfig.localdpystring);
			__vgl_safeexit(1);
		}
	}
}

extern "C" {

void *_vgl_dlopen(const char *file, int mode)
{
	globalmutex.lock(false);
	if(!__dlopen) __vgl_loaddlsymbols();
	globalmutex.unlock(false);
	checksym(dlopen);
	return __dlopen(file, mode);
}

////////////////
// X11 functions
////////////////


#ifdef sparc

Status XSolarisGetVisualGamma(Display *dpy, int screen, Visual *visual,
  double *gamma)
{
	Status ret=0;

		opentrace(XSolarisGetVisualGamma);  prargd(dpy);  prargi(screen);
		prargv(visual);  starttrace();

	if((!__vglHasGCVisuals(dpy, screen) || !fconfig.gamma_usesun)
		&& fconfig.gamma!=0.0 && fconfig.gamma!=1.0 && fconfig.gamma!=-1.0
		&& gamma)
		*gamma=1.0;
	else ret=_XSolarisGetVisualGamma(dpy, screen, visual, gamma);

		stoptrace();  if(gamma) prargf(*gamma);  closetrace();

	return ret;
}

#endif

Bool XQueryExtension(Display *dpy, _Xconst char *name, int *major_opcode,
	int *first_event, int *first_error)
{
	Bool retval=True;

	// Prevent recursion
	if(!_isremote(dpy))
		return _XQueryExtension(dpy, name, major_opcode, first_event, first_error);
	////////////////////

		opentrace(XQueryExtension);  prargd(dpy);  prargs(name);  starttrace();

	retval=_XQueryExtension(dpy, name, major_opcode, first_event, first_error);
	if(!strcmp(name, "GLX")) retval=True;

		stoptrace();  if(major_opcode) prargi(*major_opcode);
		if(first_event) prargi(*first_event);
		if(first_error) prargi(*first_error);  closetrace();

 	return retval;
}

char **XListExtensions(Display *dpy, int *next)
{
	char **list=NULL, *liststr=NULL;  int n, i;
	int hasglx=0, listlen=0;

	TRY();

	// Prevent recursion
	if(!_isremote(dpy)) return _XListExtensions(dpy, next);
	////////////////////

		opentrace(XListExtensions);  prargd(dpy);  starttrace();

	list=_XListExtensions(dpy, &n);
	if(list && n>0)
	{
		for(i=0; i<n; i++)
		{
			if(list[i])
			{
				listlen+=strlen(list[i])+1;
				if(!strcmp(list[i], "GLX")) hasglx=1;
			}
		}
	}
	if(!hasglx)
	{
		char **newlist=NULL;  int listndx=0;
		listlen+=4;  // "GLX" + terminating NULL
		errifnot(newlist=(char **)malloc(sizeof(char *)*(n+1)))
		errifnot(liststr=(char *)malloc(listlen+1))
		memset(liststr, 0, listlen+1);
		liststr=&liststr[1];  // For compatibility with X.org implementation
		if(list && n>0)
		{
			for(i=0; i<n; i++)
			{
				newlist[i]=&liststr[listndx];
				if(list[i])
				{
					strncpy(newlist[i], list[i], strlen(list[i]));
					listndx+=strlen(list[i]);
					liststr[listndx]='\0';  listndx++;
				}
			}
			XFreeExtensionList(list);
		}
		newlist[n]=&liststr[listndx];
		strncpy(newlist[n], "GLX", 3);  newlist[n][3]='\0';
		list=newlist;  n++;
	}

		stoptrace();  prargi(n);  closetrace();

	CATCH();

	if(next) *next=n;
 	return list;
}

char *XServerVendor(Display *dpy)
{
	if(strlen(fconfig.vendor)>0) return fconfig.vendor;
	else return _XServerVendor(dpy);
}

Display *XOpenDisplay(_Xconst char* name)
{
	Display *dpy=NULL;
	TRY();

		opentrace(XOpenDisplay);  prargs(name);  starttrace();

	__vgl_fakerinit();
	dpy=_XOpenDisplay(name);
	if(dpy && strlen(fconfig.vendor)>0) ServerVendor(dpy)=strdup(fconfig.vendor);

		stoptrace();  prargd(dpy);  closetrace();

	CATCH();
	return dpy;
}

int XCloseDisplay(Display *dpy)
{
	int retval=0;
	TRY();

		opentrace(XCloseDisplay);  prargd(dpy);  starttrace();

	winh.remove(dpy);
	retval=_XCloseDisplay(dpy);

		stoptrace();  closetrace();

	CATCH();
	return retval;
}

Window XCreateWindow(Display *dpy, Window parent, int x, int y,
	unsigned int width, unsigned int height, unsigned int border_width,
	int depth, unsigned int c_class, Visual *visual, unsigned long valuemask,
	XSetWindowAttributes *attributes)
{
	Window win=0;
	TRY();

		opentrace(XCreateWindow);  prargd(dpy);  prargx(parent);  prargi(x);
		prargi(y);  prargi(width);  prargi(height);  prargi(depth);
		prargi(c_class);  prargv(visual);  starttrace();

	win=_XCreateWindow(dpy, parent, x, y, width, height, border_width,
		depth, c_class, visual, valuemask, attributes);
	if(win)
	{
		if(_isremote(dpy)) winh.add(dpy, win);
		Atom deleteatom=XInternAtom(dpy, "WM_DELETE_WINDOW", True);
		if(deleteatom) XSetWMProtocols(dpy, win, &deleteatom, 1);
	}

		stoptrace();  prargx(win);  closetrace();

	CATCH();
	return win;
}

Window XCreateSimpleWindow(Display *dpy, Window parent, int x, int y,
	unsigned int width, unsigned int height, unsigned int border_width,
	unsigned long border, unsigned long background)
{
	Window win=0;
	TRY();

		opentrace(XCreateSimpleWindow);  prargd(dpy);  prargx(parent);  prargi(x);
		prargi(y);  prargi(width);  prargi(height);  starttrace();

	win=_XCreateSimpleWindow(dpy, parent, x, y, width, height, border_width,
		border, background);
	if(win)
	{
		if(_isremote(dpy)) winh.add(dpy, win);
		Atom deleteatom=XInternAtom(dpy, "WM_DELETE_WINDOW", True);
		if(deleteatom) XSetWMProtocols(dpy, win, &deleteatom, 1);
	}

		stoptrace();  prargx(win);  closetrace();

	CATCH();
	return win;
}

static void DeleteWindow(Display *dpy, Window win, bool subonly=false)
{
	Window root, parent, *children=NULL;  unsigned int n=0;

	if(!subonly) winh.remove(dpy, win);
	if(XQueryTree(dpy, win, &root, &parent, &children, &n)
		&& children && n>0)
	{
		for(unsigned int i=0; i<n; i++) DeleteWindow(dpy, children[i]);
	}
}

int XDestroyWindow(Display *dpy, Window win)
{
	int retval=0;
	TRY();

		opentrace(XDestroyWindow);  prargd(dpy);  prargx(win);  starttrace();

	DeleteWindow(dpy, win);
	retval=_XDestroyWindow(dpy, win);

		stoptrace();  closetrace();

	CATCH();
	return retval;
}

int XDestroySubwindows(Display *dpy, Window win)
{
	int retval=0;
	TRY();

		opentrace(XDestroySubwindows);  prargd(dpy);  prargx(win);  starttrace();

	DeleteWindow(dpy, win, true);
	retval=_XDestroySubwindows(dpy, win);

		stoptrace();  closetrace();

	CATCH();
	return retval;
}


Status XGetGeometry(Display *display, Drawable drawable, Window *root, int *x,
	int *y, unsigned int *width, unsigned int *height,
	unsigned int *border_width, unsigned int *depth)
{
	Status ret=0;
	unsigned int w=0, h=0;

		opentrace(XGetGeometry);  prargx(display);  prargx(drawable);
		starttrace();

	ret=_XGetGeometry(display, drawable, root, x, y, &w, &h, border_width,
		depth);
	pbwin *pbw=NULL;
	if(winh.findpb(display, drawable, pbw) && w>0 && h>0)
		pbw->resize(w, h);

		stoptrace();  if(root) prargx(*root);  if(x) prargi(*x);  if(y) prargi(*y);
		prargi(w);  prargi(h);
		if(border_width) prargi(*border_width);  if(depth) prargi(*depth);
		closetrace();

	if(width) *width=w;  if(height) *height=h;
	return ret;
}

static void _HandleEvent(Display *dpy, XEvent *xe)
{
	pbwin *pbw=NULL;
	if(xe && xe->type==ConfigureNotify)
	{
		if(winh.findpb(dpy, xe->xconfigure.window, pbw))
		{
				opentrace(_HandleEvent);  prargi(xe->xconfigure.width);
				prargi(xe->xconfigure.height);  prargx(xe->xconfigure.window);
				starttrace();

			pbw->resize(xe->xconfigure.width, xe->xconfigure.height);

				stoptrace();  closetrace();
		}
	}
	else if(xe && xe->type==KeyPress)
	{
		unsigned int state2, state=(xe->xkey.state)&(~(LockMask));
		state2=fconfig.guimod;
		if(state2&Mod1Mask) {state2&=(~(Mod1Mask));  state2|=Mod2Mask;}
		if(fconfig.gui
			&& XKeycodeToKeysym(dpy, xe->xkey.keycode, 0)==fconfig.guikey
			&& (state==fconfig.guimod || state==state2)
			&& fconfig_getshmid()!=-1)
			vglpopup(dpy, fconfig_getshmid());
	}
	else if(xe && xe->type==ClientMessage)
	{
		XClientMessageEvent *cme=(XClientMessageEvent *)xe;
		Atom protoatom=XInternAtom(dpy, "WM_PROTOCOLS", True);
		Atom deleteatom=XInternAtom(dpy, "WM_DELETE_WINDOW", True);
		if(protoatom && deleteatom && cme->message_type==protoatom
			&& cme->data.l[0]==(long)deleteatom
			&& winh.findpb(dpy, cme->window, pbw))
			pbw->wmdelete();
	}
}

int XNextEvent(Display *dpy, XEvent *xe)
{
	int retval=0;
	TRY();
	retval=_XNextEvent(dpy, xe);
	_HandleEvent(dpy, xe);
	CATCH();
	return retval;
}

int XWindowEvent(Display *dpy, Window win, long event_mask, XEvent *xe)
{
	int retval=0;
	TRY();
	retval=_XWindowEvent(dpy, win, event_mask, xe);
	_HandleEvent(dpy, xe);
	CATCH();
	return retval;
}

Bool XCheckWindowEvent(Display *dpy, Window win, long event_mask, XEvent *xe)
{
	Bool retval=0;
	TRY();
	if((retval=_XCheckWindowEvent(dpy, win, event_mask, xe))==True)
		_HandleEvent(dpy, xe);
	CATCH();
	return retval;
}

int XMaskEvent(Display *dpy, long event_mask, XEvent *xe)
{
	int retval=0;
	TRY();
	retval=_XMaskEvent(dpy, event_mask, xe);
	_HandleEvent(dpy, xe);
	CATCH();
	return retval;
}

Bool XCheckMaskEvent(Display *dpy, long event_mask, XEvent *xe)
{
	Bool retval=0;
	TRY();
	if((retval=_XCheckMaskEvent(dpy, event_mask, xe))==True)
		_HandleEvent(dpy, xe);
	CATCH();
	return retval;
}

Bool XCheckTypedEvent(Display *dpy, int event_type, XEvent *xe)
{
	Bool retval=0;
	TRY();
	if((retval=_XCheckTypedEvent(dpy, event_type, xe))==True)
		_HandleEvent(dpy, xe);
	CATCH();
	return retval;
}

Bool XCheckTypedWindowEvent(Display *dpy, Window win, int event_type,
	XEvent *xe)
{
	Bool retval=0;
	TRY();
	if((retval=_XCheckTypedWindowEvent(dpy, win, event_type, xe))==True)
		_HandleEvent(dpy, xe);
	CATCH();
	return retval;
}

int XConfigureWindow(Display *dpy, Window win, unsigned int value_mask,
	XWindowChanges *values)
{
	int retval=0;
	TRY();

		opentrace(XConfigureWindow);  prargd(dpy);  prargx(win);
		if(values && (value_mask&CWWidth)) {prargi(values->width);}
		if(values && (value_mask&CWHeight)) {prargi(values->height);}
		starttrace();

	pbwin *pbw=NULL;
	if(winh.findpb(dpy, win, pbw) && values)
		pbw->resize(value_mask&CWWidth? values->width:0,
			value_mask&CWHeight?values->height:0);
	retval=_XConfigureWindow(dpy, win, value_mask, values);

		stoptrace();  closetrace();

	CATCH();
	return retval;
}

int XResizeWindow(Display *dpy, Window win, unsigned int width,
	unsigned int height)
{
	int retval=0;
	TRY();

		opentrace(XResizeWindow);  prargd(dpy);  prargx(win);  prargi(width);
		prargi(height);  starttrace();

	pbwin *pbw=NULL;
	if(winh.findpb(dpy, win, pbw)) pbw->resize(width, height);
	retval=_XResizeWindow(dpy, win, width, height);

		stoptrace();  closetrace();

	CATCH();
	return retval;
}

int XMoveResizeWindow(Display *dpy, Window win, int x, int y,
	unsigned int width, unsigned int height)
{
	int retval=0;
	TRY();

		opentrace(XMoveResizeWindow);  prargd(dpy);  prargx(win);  prargi(x);
		prargi(y);  prargi(width);  prargi(height);  starttrace();

	pbwin *pbw=NULL;
	if(winh.findpb(dpy, win, pbw)) pbw->resize(width, height);
	retval=_XMoveResizeWindow(dpy, win, x, y, width, height);

		stoptrace();  closetrace();

	CATCH();
	return retval;
}

// We have to override this function to handle GLX pixmap rendering
int XCopyArea(Display *dpy, Drawable src, Drawable dst, GC gc, int src_x,
	int src_y, unsigned int w, unsigned int h, int dest_x, int dest_y)
{
	TRY();
	pbdrawable *pbsrc=NULL;  pbdrawable *pbdst=NULL;
	bool srcwin=false, dstwin=false;
	bool copy2d=true, copy3d=false, triggerrb=false;
	int retval=0;
	GLXDrawable glxsrc=0, glxdst=0;

	if(src==0 || dst==0) return BadDrawable;

		opentrace(XCopyArea);  prargd(dpy);  prargx(src);  prargx(dst);
		prargx(gc); prargi(src_x);  prargi(src_y);  prargi(w);  prargi(h);
		prargi(dest_x);  prargi(dest_y);  starttrace();

	if(!(pbsrc=(pbdrawable *)pmh.find(dpy, src)))
	{
		pbsrc=(pbdrawable *)winh.findwin(dpy, src);
		if(pbsrc) srcwin=true;
	};
	if(!(pbdst=(pbdrawable *)pmh.find(dpy, dst)))
	{
		pbdst=(pbdrawable *)winh.findwin(dpy, dst);
		if(pbdst) dstwin=true;
	}

	// GLX (Pbuffer-backed) Pixmap --> non-GLX drawable
	// Sync pixels from the Pbuffer backing the Pixmap to the actual Pixmap and
	// let the "real" XCopyArea() do the rest.
	if(pbsrc && !srcwin && !pbdst) ((pbpm *)pbsrc)->readback();

	// non-GLX drawable --> non-GLX drawable
	// Source and destination are not backed by a Pbuffer, so defer to the
	// real XCopyArea() function.
	//
	// non-GLX drawable --> GLX (Pbuffer-backed) drawable
	// We don't really handle this yet (and won't until we have to.)  Copy to the
	// X11 destination drawable only, without updating the corresponding Pbuffer.
	//
	// GLX (Pbuffer-backed) Window --> non-GLX drawable
	// We assume that glFinish() or another synchronization function has been
	// called prior to XCopyArea(), so we defer to the real XCopyArea() function
	// (but this may not work properly without VGL_SYNC=1.)
	{}

	// GLX (Pbuffer-backed) Window --> GLX (Pbuffer-backed) drawable
	// GLX (Pbuffer-backed) Pixmap --> GLX (Pbuffer-backed) Pixmap
	// Sync both 2D and 3D pixels.
	if(pbsrc && srcwin && pbdst) copy3d=true;
	if(pbsrc && !srcwin && pbdst && !dstwin) copy3d=true;

	// GLX (Pbuffer-backed) Pixmap --> GLX (Pbuffer-backed) Window
	// Copy 3D pixels to the destination Pbuffer, then trigger a VirtualGL
	// readback to deliver the pixels to the window.
	if(pbsrc && !srcwin && pbdst && dstwin)
	{
		copy2d=false;  copy3d=true;  triggerrb=true;
	}

	if(copy2d) retval=_XCopyArea(dpy, src, dst, gc, src_x, src_y, w, h, dest_x,
		dest_y);

	if(copy3d)
	{
		glxsrc=pbsrc->getglxdrawable();
		glxdst=pbdst->getglxdrawable();
		pbsrc->copypixels(src_x, src_y, w, h, dest_x, dest_y, glxdst);
		if(triggerrb) ((pbwin *)pbdst)->readback(GL_FRONT, false, fconfig.sync);
	}

		stoptrace();  if(copy3d) prargx(glxsrc);  if(copy3d) prargx(glxdst);
		closetrace();

	CATCH();
	return 0;
}

int XFree(void *data)
{
	int ret=0;
	TRY();
	ret=_XFree(data);
	if(data && !isdead()) vish.remove(NULL, (XVisualInfo *)data);
	CATCH();
	return ret;
}

/////////////////////////////
// GLX 1.0 Context management
/////////////////////////////

XVisualInfo *glXChooseVisual(Display *dpy, int screen, int *attrib_list)
{
	XVisualInfo *v=NULL;
	static bool alreadywarned=false;
	TRY();

	// Prevent recursion
	if(!_isremote(dpy)) return _glXChooseVisual(dpy, screen, attrib_list);
	////////////////////

		opentrace(glXChooseVisual);  prargd(dpy);  prargi(screen);
		prargal11(attrib_list);  starttrace();


	if(attrib_list)
	{
		bool overlayreq=false;
		for(int i=0; attrib_list[i]!=None && i<=254; i++)
		{
			if(attrib_list[i]==GLX_DOUBLEBUFFER || attrib_list[i]==GLX_RGBA
				|| attrib_list[i]==GLX_STEREO || attrib_list[i]==GLX_USE_GL)
				continue;
			else if(attrib_list[i]==GLX_LEVEL && attrib_list[i+1]==1)
			{
				overlayreq=true;  i++;
			}
			else i++;
		}
		if(overlayreq)
		{
			int dummy;
			if(!_XQueryExtension(dpy, "GLX", &dummy, &dummy, &dummy))
				v=NULL;
			else v=_glXChooseVisual(dpy, screen, attrib_list);
			stoptrace();  prargv(v);  closetrace();
			return v;
		}
	}


	GLXFBConfig *configs=NULL, c=0, cprev;  int n=0;
	if(!dpy || !attrib_list) return NULL;
	int depth=24, c_class=TrueColor, level=0, stereo=0, trans=0;
	if(!(configs=__vglConfigsFromVisAttribs(attrib_list, depth, c_class,
		level, stereo, trans, n)) || n<1)
	{
		if(!alreadywarned && fconfig.verbose)
		{
			alreadywarned=true;
			rrout.println("[VGL] WARNING: VirtualGL attempted and failed to obtain a Pbuffer-enabled");
			rrout.println("[VGL]    24-bit visual on the 3D X server %s.  This is normal if", fconfig.localdpystring);
			rrout.println("[VGL]    the 3D application is probing for visuals with certain capabilities,");
			rrout.println("[VGL]    but if the app fails to start, then make sure that the 3D X server is");
			rrout.println("[VGL]    configured for 24-bit color and has accelerated 3D drivers installed.");
		}
		return NULL;
	}
	c=configs[0];
	XFree(configs);
	VisualID vid=__vglMatchVisual(dpy, screen, depth, c_class, level, stereo, trans);
	if(!vid) return NULL;
	v=__vglVisualFromVisualID(dpy, screen, vid);
	if(!v) return NULL;

	if((cprev=vish.getpbconfig(dpy, v)) && _FBCID(c) != _FBCID(cprev)
		&& fconfig.trace)
		rrout.println("[VGL] WARNING: Visual 0x%.2x was previously mapped to FB config 0x%.2x and is now mapped to 0x%.2x\n",
			v->visualid, _FBCID(cprev), _FBCID(c));

	vish.add(dpy, v, c);

		stoptrace();  prargv(v);  prargc(c);  closetrace();

	CATCH();
	return v;
}

XVisualInfo *glXGetVisualFromFBConfig(Display *dpy, GLXFBConfig config)
{
	XVisualInfo *v=NULL;
	TRY();

	// Prevent recursion
	if(!_isremote(dpy)) return _glXGetVisualFromFBConfig(dpy, config);
	////////////////////

		opentrace(glXGetVisualFromFBConfig);  prargd(dpy);  prargc(config);
		starttrace();

	if(rcfgh.isoverlay(dpy, config))
	{
		v=_glXGetVisualFromFBConfig(dpy, config);
		stoptrace();  prargv(v);  closetrace();
		return v;
	}

	VisualID vid=0;
	if(!dpy || !config) return NULL;
	vid=_MatchVisual(dpy, config);
	if(!vid) return NULL;
	v=__vglVisualFromVisualID(dpy, DefaultScreen(dpy), vid);
	if(!v) return NULL;
	vish.add(dpy, v, config);

		stoptrace();  prargv(v);  closetrace();

	CATCH();
	return v;
}

XVisualInfo *glXGetVisualFromFBConfigSGIX(Display *dpy, GLXFBConfigSGIX config)
{
	return glXGetVisualFromFBConfig(dpy, config);
}

GLXContext glXCreateContext(Display *dpy, XVisualInfo *vis,
	GLXContext share_list, Bool direct)
{
	GLXContext ctx=0;
	TRY();

	// Prevent recursion
	if(!_isremote(dpy)) return _glXCreateContext(dpy, vis, share_list, direct);
	////////////////////

		opentrace(glXCreateContext);  prargd(dpy);  prargv(vis);
		prargx(share_list);  prargi(direct);  starttrace();

	if(!fconfig.allowindirect) direct=True;

	if(vis)
	{
		int level=__vglClientVisualAttrib(dpy, DefaultScreen(dpy), vis->visualid,
			GLX_LEVEL);
		int trans=(__vglClientVisualAttrib(dpy, DefaultScreen(dpy), vis->visualid,
			GLX_TRANSPARENT_TYPE)==GLX_TRANSPARENT_INDEX);
		if(level && trans)
		{
			int dummy;
			if(!_XQueryExtension(dpy, "GLX", &dummy, &dummy, &dummy))
				ctx=NULL;
			else ctx=_glXCreateContext(dpy, vis, share_list, direct);
			if(ctx) ctxh.add(ctx, (GLXFBConfig)-1);
			stoptrace();  prargx(ctx);  closetrace();
			return ctx;
		}
	}

	GLXFBConfig c;
	if(!(c=_MatchConfig(dpy, vis)))
		_throw("Could not obtain Pbuffer-capable RGB visual on the server");
	ctx=_glXCreateNewContext(_localdpy, c, GLX_RGBA_TYPE, share_list, direct);
	if(ctx)
	{
		if(!_glXIsDirect(_localdpy, ctx) && direct)
		{
			rrout.println("[VGL] WARNING: The OpenGL rendering context obtained on X display");
			rrout.println("[VGL]    %s is indirect, which may cause performance to suffer.",
				DisplayString(_localdpy));
			rrout.println("[VGL]    If %s is a local X display, then the framebuffer device",
				DisplayString(_localdpy));
			rrout.println("[VGL]    permissions may be set incorrectly.");
		}
		ctxh.add(ctx, c);
	}

		stoptrace();  prargc(c);  prargx(ctx);  closetrace();

	CATCH();
	return ctx;
}

Bool glXMakeCurrent(Display *dpy, GLXDrawable drawable, GLXContext ctx)
{
	Bool retval=0;  const char *renderer="Unknown";
	TRY();
	pbwin *pbw;  GLXFBConfig config=0;

	// Prevent recursion
	if(!_isremote(dpy)) return _glXMakeCurrent(dpy, drawable, ctx);
	////////////////////

		opentrace(glXMakeCurrent);  prargd(dpy);  prargx(drawable);  prargx(ctx);
		starttrace();

	if(ctx) config=ctxh.findconfig(ctx);
	if(config==(GLXFBConfig)-1)
	{
		retval=_glXMakeCurrent(dpy, drawable, ctx);
		winh.setoverlay(dpy, drawable);
		stoptrace();  closetrace();
		return retval;
	}

	// Equivalent of a glFlush()
	GLXDrawable curdraw=_glXGetCurrentDrawable();
	if(glXGetCurrentContext() && _localdisplayiscurrent()
		&& curdraw && winh.findpb(curdraw, pbw))
	{
		pbwin *newpbw;
		if(drawable==0 || !winh.findpb(dpy, drawable, newpbw)
			|| newpbw->getglxdrawable()!=curdraw)
		{
			if(_drawingtofront() || pbw->_dirty)
				pbw->readback(GL_FRONT, false, false);
		}
	}

	// If the drawable isn't a window, we pass it through unmodified, else we
	// map it to a Pbuffer
	if(dpy && drawable && ctx)
	{
		if(!config)
		{
			rrout.PRINTLN("[VGL] WARNING: glXMakeCurrent() called with a previously-destroyed context.");
			return False;
		}
		pbw=winh.setpb(dpy, drawable, config);
		if(pbw) drawable=pbw->updatedrawable();
		else if(!glxdh.getcurrentdpy(drawable))
		{
			// Apparently it isn't a Pbuffer or a Pixmap, so it must be a window
			// that was created in another application.  This code is necessary
			// to make CRUT (Chromium Utility Toolkit) applications work.
			if(_isremote(dpy))
			{
				winh.add(dpy, drawable);
				pbw=winh.setpb(dpy, drawable, config);
				if(pbw)
					drawable=pbw->updatedrawable();
			}
		}
	}

	retval=_glXMakeContextCurrent(_localdpy, drawable, drawable, ctx);
	if(fconfig.trace && retval) renderer=(const char *)glGetString(GL_RENDERER);
	if(winh.findpb(drawable, pbw)) {pbw->clear();  pbw->cleanup();}
	pbpm *pbp;
	if((pbp=pmh.find(dpy, drawable))!=NULL) pbp->clear();
	#ifdef SUNOGL
	sunOglCurPrimTablePtr->oglIndexd=r_glIndexd;
	sunOglCurPrimTablePtr->oglIndexf=r_glIndexf;
	sunOglCurPrimTablePtr->oglIndexi=r_glIndexi;
	sunOglCurPrimTablePtr->oglIndexs=r_glIndexs;
	sunOglCurPrimTablePtr->oglIndexub=r_glIndexub;
	sunOglCurPrimTablePtr->oglIndexdv=r_glIndexdv;
	sunOglCurPrimTablePtr->oglIndexfv=r_glIndexfv;
	sunOglCurPrimTablePtr->oglIndexiv=r_glIndexiv;
	sunOglCurPrimTablePtr->oglIndexsv=r_glIndexsv;
	sunOglCurPrimTablePtr->oglIndexubv=r_glIndexubv;
	#endif

		stoptrace();  prargc(config);  prargx(drawable);  prargs(renderer);
		closetrace();

	CATCH();
	return retval;
}

void glXDestroyContext(Display* dpy, GLXContext ctx)
{
	TRY();

		opentrace(glXDestroyContext);  prargd(dpy);  prargx(ctx);  starttrace();

	if(ctxh.isoverlay(ctx))
	{
		_glXDestroyContext(dpy, ctx);
		stoptrace();  closetrace();
		return;
	}

	ctxh.remove(ctx);
	_glXDestroyContext(_localdpy, ctx);

		stoptrace();  closetrace();

	CATCH();
}

/////////////////////////////
// GLX 1.3 Context management
/////////////////////////////

GLXContext glXCreateNewContext(Display *dpy, GLXFBConfig config,
	int render_type, GLXContext share_list, Bool direct)
{
	GLXContext ctx=0;
	TRY();

	// Prevent recursion
	if(!_isremote(dpy))
		return _glXCreateNewContext(dpy, config, render_type, share_list, direct);
	////////////////////

		opentrace(glXCreateNewContext);  prargd(dpy);  prargc(config);
		prargi(render_type);  prargx(share_list);  prargi(direct);  starttrace();

	if(!fconfig.allowindirect) direct=True;

	if(rcfgh.isoverlay(dpy, config)) // Overlay config
	{
		ctx=_glXCreateNewContext(dpy, config, render_type, share_list, direct);
		if(ctx) ctxh.add(ctx, (GLXFBConfig)-1);
		stoptrace();  prargx(ctx);  closetrace();
		return ctx;
	}

	ctx=_glXCreateNewContext(_localdpy, config, GLX_RGBA_TYPE, share_list, direct);
	if(ctx)
	{
		if(!_glXIsDirect(_localdpy, ctx) && direct)
		{
			rrout.println("[VGL] WARNING: The OpenGL rendering context obtained on X display");
			rrout.println("[VGL]    %s is indirect, which may cause performance to suffer.",
				DisplayString(_localdpy));
			rrout.println("[VGL]    If %s is a local X display, then the framebuffer device",
				DisplayString(_localdpy));
			rrout.println("[VGL]    permissions may be set incorrectly.");
		}
		ctxh.add(ctx, config);
	}

		stoptrace();  prargx(ctx);  closetrace();

	CATCH();
	return ctx;
}

Bool glXMakeContextCurrent(Display *dpy, GLXDrawable draw, GLXDrawable read,
	GLXContext ctx)
{
	Bool retval=0;  const char *renderer="Unknown";
	pbwin *pbw;  GLXFBConfig config=0;
	TRY();

	// Prevent recursion
	if(!_isremote(dpy)) return _glXMakeContextCurrent(dpy, draw, read, ctx);
	////////////////////

		opentrace(glXMakeContextCurrent);  prargd(dpy);  prargx(draw);
		prargx(read);  prargx(ctx);  starttrace();

	if(ctx) config=ctxh.findconfig(ctx);
	if(config==(GLXFBConfig)-1)
	{
		retval=_glXMakeContextCurrent(dpy, draw, read, ctx);
		winh.setoverlay(dpy, draw);
		winh.setoverlay(dpy, read);
		stoptrace();  closetrace();
		return retval;
	}

	// Equivalent of a glFlush()
	GLXDrawable curdraw=_glXGetCurrentDrawable();
	if(glXGetCurrentContext() && _localdisplayiscurrent()
	&& curdraw && winh.findpb(curdraw, pbw))
	{
		pbwin *newpbw;
		if(draw==0 || !winh.findpb(dpy, draw, newpbw)
			|| newpbw->getglxdrawable()!=curdraw)
		{
			if(_drawingtofront() || pbw->_dirty)
				pbw->readback(GL_FRONT, false, false);
		}
	}

	// If the drawable isn't a window, we pass it through unmodified, else we
	// map it to a Pbuffer
	pbwin *drawpbw, *readpbw;
	if(dpy && (draw || read) && ctx)
	{
		if(!config)
		{
			rrout.PRINTLN("[VGL] WARNING: glXMakeContextCurrent() called with a previously-destroyed context");
			return False;
		}
		drawpbw=winh.setpb(dpy, draw, config);
		readpbw=winh.setpb(dpy, read, config);
		if(drawpbw)
			draw=drawpbw->updatedrawable();
		if(readpbw) read=readpbw->updatedrawable();
	}
	retval=_glXMakeContextCurrent(_localdpy, draw, read, ctx);
	if(fconfig.trace && retval) renderer=(const char *)glGetString(GL_RENDERER);
	if(winh.findpb(draw, drawpbw)) {drawpbw->clear();  drawpbw->cleanup();}
	if(winh.findpb(read, readpbw)) readpbw->cleanup();
	pbpm *pbp;
	if((pbp=pmh.find(dpy, draw))!=NULL) pbp->clear();
	#ifdef SUNOGL
	sunOglCurPrimTablePtr->oglIndexd=r_glIndexd;
	sunOglCurPrimTablePtr->oglIndexf=r_glIndexf;
	sunOglCurPrimTablePtr->oglIndexi=r_glIndexi;
	sunOglCurPrimTablePtr->oglIndexs=r_glIndexs;
	sunOglCurPrimTablePtr->oglIndexub=r_glIndexub;
	sunOglCurPrimTablePtr->oglIndexdv=r_glIndexdv;
	sunOglCurPrimTablePtr->oglIndexfv=r_glIndexfv;
	sunOglCurPrimTablePtr->oglIndexiv=r_glIndexiv;
	sunOglCurPrimTablePtr->oglIndexsv=r_glIndexsv;
	sunOglCurPrimTablePtr->oglIndexubv=r_glIndexubv;
	#endif

		stoptrace();  prargc(config);  prargx(draw);  prargx(read);
		prargs(renderer);  closetrace();

	CATCH();
	return retval;
}

Bool glXMakeCurrentReadSGI(Display *dpy, GLXDrawable draw, GLXDrawable read,
	GLXContext ctx)
{
	return glXMakeContextCurrent(dpy, draw, read, ctx);
}

/////////////////////////////
// GLX 1.4 Context management
/////////////////////////////

GLXContext glXCreateContextAttribsARB(Display *dpy, GLXFBConfig config,
	GLXContext share_context, Bool direct, const int *attribs)
{
	GLXContext ctx=0;
	TRY();

	// Prevent recursion
	if(!_isremote(dpy))
		return _glXCreateContextAttribsARB(dpy, config, share_context, direct,
			attribs);
	////////////////////

		opentrace(glXCreateContextAttribsARB);  prargd(dpy);  prargc(config);
		prargx(share_context);  prargi(direct);  prargal13(attribs);
		starttrace();

	if(!fconfig.allowindirect) direct=True;

	if(rcfgh.isoverlay(dpy, config)) // Overlay config
	{
		ctx=_glXCreateContextAttribsARB(dpy, config, share_context, direct,
			attribs);
		if(ctx) ctxh.add(ctx, (GLXFBConfig)-1);
		stoptrace();  prargx(ctx);  closetrace();
		return ctx;
	}

	if(attribs)
	{
		for(int i=0; attribs[i]!=None && i<=254; i+=2)
		{
			if(attribs[i]==GLX_RENDER_TYPE) ((int *)attribs)[i+1]=GLX_RGBA_TYPE;
		}
	}

	ctx=_glXCreateContextAttribsARB(_localdpy, config, share_context, direct,
		attribs);
	if(ctx)
	{
		if(!_glXIsDirect(_localdpy, ctx) && direct)
		{
			rrout.println("[VGL] WARNING: The OpenGL rendering context obtained on X display");
			rrout.println("[VGL]    %s is indirect, which may cause performance to suffer.",
				DisplayString(_localdpy));
			rrout.println("[VGL]    If %s is a local X display, then the framebuffer device",
				DisplayString(_localdpy));
			rrout.println("[VGL]    permissions may be set incorrectly.");
		}
		ctxh.add(ctx, config);
	}

		stoptrace();  prargx(ctx);  closetrace();

	CATCH();
	return ctx;
}

///////////////////////////////////
// SGIX_fbconfig Context management
///////////////////////////////////

// On Linux, GLXFBConfigSGIX is typedef'd to GLXFBConfig
GLXContext glXCreateContextWithConfigSGIX(Display *dpy, GLXFBConfigSGIX config,
	int render_type, GLXContext share_list, Bool direct)
{
	return glXCreateNewContext(dpy, config, render_type, share_list, direct);
}

/////////////////////////
// Other GL/GLX functions
/////////////////////////

// Here, we fake out the client into thinking it's getting a window drawable,
// but really it's getting a Pbuffer drawable
GLXWindow glXCreateWindow(Display *dpy, GLXFBConfig config, Window win,
	const int *attrib_list)
{
	// Prevent recursion
	if(!_isremote(dpy)) return _glXCreateWindow(dpy, config, win, attrib_list);
	////////////////////

	TRY();

		opentrace(glXCreateWindow);  prargd(dpy);  prargc(config);  prargx(win);
		starttrace();

	pbwin *pbw=NULL;
	if(rcfgh.isoverlay(dpy, config))
	{
		GLXWindow glxw=_glXCreateWindow(dpy, config, win, attrib_list);
		winh.setoverlay(dpy, glxw);
	}
	else
	{
		XSync(dpy, False);
		errifnot(pbw=winh.setpb(dpy, win, config));
	}

		stoptrace();  if(pbw) {prargx(pbw->getglxdrawable());}  closetrace();

	CATCH();
	return win;  // Make the client store the original window handle, which we
               // use to find the Pbuffer in the hash
}

void glXDestroyWindow(Display *dpy, GLXWindow win)
{
	TRY();
	// Prevent recursion
	if(!_isremote(dpy)) {_glXDestroyWindow(dpy, win);  return;}
	////////////////////

		opentrace(glXDestroyWindow);  prargd(dpy);  prargx(win);  starttrace();

	if(winh.isoverlay(dpy, win)) _glXDestroyWindow(dpy, win);  
	winh.remove(dpy, win);

		stoptrace();  closetrace();

	CATCH();
}

// Pixmap rendering, another shameless hack.  What we're really returning is a
// Pbuffer handle

GLXPixmap glXCreateGLXPixmap(Display *dpy, XVisualInfo *vi, Pixmap pm)
{
	GLXPixmap drawable=0;
	TRY();
	GLXFBConfig c;

	// Prevent recursion
	if(!_isremote(dpy)) return _glXCreateGLXPixmap(dpy, vi, pm);
	////////////////////

		opentrace(glXCreateGLXPixmap);  prargd(dpy);  prargv(vi);  prargx(pm);
		starttrace();

	if(vi)
	{
		int level=__vglClientVisualAttrib(dpy, DefaultScreen(dpy), vi->visualid,
			GLX_LEVEL);
		int trans=(__vglClientVisualAttrib(dpy, DefaultScreen(dpy), vi->visualid,
			GLX_TRANSPARENT_TYPE)==GLX_TRANSPARENT_INDEX);
		if(level && trans)
		{
			int dummy;
			if(!_XQueryExtension(dpy, "GLX", &dummy, &dummy, &dummy))
				drawable=0;
			else drawable=_glXCreateGLXPixmap(dpy, vi, pm);
			stoptrace();  prargx(drawable);  closetrace();
			return drawable;
		}
	}

	Window root;  int x, y;  unsigned int w, h, bw, d;
	XGetGeometry(dpy, pm, &root, &x, &y, &w, &h, &bw, &d);
	if(!(c=_MatchConfig(dpy, vi)))
		_throw("Could not obtain Pbuffer-capable RGB visual on the server");
	pbpm *pbp=new pbpm(dpy, pm, vi->visual);
	if(pbp)
	{
		pbp->init(w, h, c);
		pmh.add(dpy, pm, pbp);
		glxdh.add(pbp->getglxdrawable(), dpy);
		drawable=pbp->getglxdrawable();
	}

		stoptrace();  prargi(x);  prargi(y);  prargi(w);  prargi(h);
		prargi(d);  prargc(c);  prargx(drawable);  closetrace();

	CATCH();
	return drawable;
}

void glXDestroyGLXPixmap(Display *dpy, GLXPixmap pix)
{
	TRY();
	// Prevent recursion
	if(!_isremote(dpy)) {_glXDestroyGLXPixmap(dpy, pix);  return;}
	////////////////////

		opentrace(glXDestroyGLXPixmap);  prargd(dpy);  prargx(pix);  starttrace();

	// Sync the contents of the Pbuffer with the real Pixmap before we delete
	// the Pbuffer
	pbpm *pbp=pmh.find(dpy, pix);
	if(pbp) pbp->readback();

	glxdh.remove(pix);
	pmh.remove(dpy, pix);

		stoptrace();  closetrace();

	CATCH();
}

GLXPixmap glXCreatePixmap(Display *dpy, GLXFBConfig config, Pixmap pm,
	const int *attribs)
{
	GLXPixmap drawable=0;
	TRY();

	// Prevent recursion && handle overlay configs
	if(!_isremote(dpy) || rcfgh.isoverlay(dpy, config))
		return _glXCreatePixmap(dpy, config, pm, attribs);
	////////////////////

		opentrace(glXCreatePixmap);  prargd(dpy);  prargc(config);  prargx(pm);
		starttrace();

	Window root;  int x, y;  unsigned int w, h, bw, d;
	XGetGeometry(dpy, pm, &root, &x, &y, &w, &h, &bw, &d);

	VisualID vid=_MatchVisual(dpy, config);
	pbpm *pbp=NULL;
	if(vid)
	{
		XVisualInfo *v=__vglVisualFromVisualID(dpy, DefaultScreen(dpy), vid);
		if(v) pbp=new pbpm(dpy, pm, v->visual);
	}
	if(pbp)
	{
		pbp->init(w, h, config);
		pmh.add(dpy, pm, pbp);
		glxdh.add(pbp->getglxdrawable(), dpy);
		drawable=pbp->getglxdrawable();
	}

		stoptrace();  prargi(x);  prargi(y);  prargi(w);  prargi(h);
		prargi(d);  prargx(drawable);  closetrace();

	CATCH();
	return drawable;
}

GLXPixmap glXCreateGLXPixmapWithConfigSGIX(Display *dpy,
	GLXFBConfigSGIX config, Pixmap pixmap)
{
	return glXCreatePixmap(dpy, config, pixmap, NULL);
}

void glXDestroyPixmap(Display *dpy, GLXPixmap pix)
{
	TRY();
	// Prevent recursion
	if(!_isremote(dpy)) {_glXDestroyPixmap(dpy, pix);  return;}
	////////////////////

		opentrace(glXDestroyPixmap);  prargd(dpy);  prargx(pix);  starttrace();

	// Sync the contents of the Pbuffer with the real Pixmap before we delete
	// the Pbuffer
	pbpm *pbp=pmh.find(dpy, pix);
	if(pbp) pbp->readback();

	glxdh.remove(pix);
	pmh.remove(dpy, pix);

		stoptrace();  closetrace();

	CATCH();
}

#include "xfonts.c"

// We use a tweaked out version of the Mesa glXUseXFont()
// implementation.
void glXUseXFont(Font font, int first, int count, int list_base)
{
	TRY();

		opentrace(glXUseXFont);  prargx(font);  prargi(first);  prargi(count);
		prargi(list_base);  starttrace();

	if(ctxh.overlaycurrent()) _glXUseXFont(font, first, count, list_base);
	else Fake_glXUseXFont(font, first, count, list_base);

		stoptrace();  closetrace();

	return;
	CATCH();
}

void glXSwapBuffers(Display* dpy, GLXDrawable drawable)
{
	TRY();

		opentrace(glXSwapBuffers);  prargd(dpy);  prargx(drawable);  starttrace();

	if(winh.isoverlay(dpy, drawable))
	{
		_glXSwapBuffers(dpy, drawable);
		stoptrace();  closetrace();  
		return;
	}

	fconfig.flushdelay=0.;
	pbwin *pbw=NULL;
	if(_isremote(dpy) && winh.findpb(dpy, drawable, pbw))
	{
		pbw->readback(GL_BACK, false, fconfig.sync);
		pbw->swapbuffers();
	}
	else _glXSwapBuffers(_localdpy, drawable);

		stoptrace();  if(_isremote(dpy) && pbw) {prargx(pbw->getglxdrawable());}
		closetrace();  

	CATCH();
}

static void _doGLreadback(bool spoillast, bool sync)
{
	pbwin *pbw;
	GLXDrawable drawable;
	if(ctxh.overlaycurrent()) return;
	drawable=_glXGetCurrentDrawable();
	if(!drawable) return;
	if(winh.findpb(drawable, pbw))
	{
		if(_drawingtofront() || pbw->_dirty)
		{
				opentrace(_doGLreadback);  prargx(pbw->getglxdrawable());
				prargi(sync); prargi(spoillast);  starttrace();

			pbw->readback(GL_FRONT, spoillast, sync);

				stoptrace();  closetrace();
		}
	}
}

void glFlush(void)
{
	static double lasttime=-1.;  double thistime;
	TRY();

		if(fconfig.trace) rrout.print("[VGL] glFlush()\n");

	_glFlush();
	if(lasttime<0.) lasttime=rrtime();
	else
	{
		thistime=rrtime()-lasttime;
		if(thistime-lasttime<0.01) fconfig.flushdelay=0.01;
		else fconfig.flushdelay=0.;
	}
	_doGLreadback(fconfig.spoillast, false);
	CATCH();
}

void glFinish(void)
{
	TRY();

		if(fconfig.trace) rrout.print("[VGL] glFinish()\n");

	_glFinish();
	fconfig.flushdelay=0.;
	_doGLreadback(false, fconfig.sync);
	CATCH();
}

void glXWaitGL(void)
{
	TRY();

		if(fconfig.trace) rrout.print("[VGL] glXWaitGL()\n");

	if(ctxh.overlaycurrent()) {_glXWaitGL();  return;}

	_glFinish();  // glXWaitGL() on some systems calls glFinish(), so we do this
	              // to avoid 2 readbacks
	fconfig.flushdelay=0.;
	_doGLreadback(false, fconfig.sync);
	CATCH();
}

// If the application switches the draw buffer before calling glFlush(), we
// set a lazy readback trigger
void glDrawBuffer(GLenum mode)
{
	TRY();

	if(ctxh.overlaycurrent()) {_glDrawBuffer(mode);  return;}

		opentrace(glDrawBuffer);  prargx(mode);  starttrace();

	pbwin *pbw=NULL;  int before=-1, after=-1, rbefore=-1, rafter=-1;
	GLXDrawable drawable=_glXGetCurrentDrawable();
	if(drawable && winh.findpb(drawable, pbw))
	{
		before=_drawingtofront();
		rbefore=_drawingtoright();
		_glDrawBuffer(mode);
		after=_drawingtofront();
		rafter=_drawingtoright();
		if(before && !after) pbw->_dirty=true;
		if(rbefore && !rafter && pbw->stereo()) pbw->_rdirty=true;
	}
	else _glDrawBuffer(mode);

		stoptrace();  if(drawable && pbw) {prargi(pbw->_dirty);
		prargi(pbw->_rdirty);  prargx(pbw->getglxdrawable());}  closetrace();

	CATCH();
}

// glPopAttrib() can change the draw buffer state as well :/
void glPopAttrib(void)
{
	TRY();

	if(ctxh.overlaycurrent()) {_glPopAttrib();  return;}

		opentrace(glPopAttrib);  starttrace();

	pbwin *pbw=NULL;  int before=-1, after=-1, rbefore=-1, rafter=-1;
	GLXDrawable drawable=_glXGetCurrentDrawable();
	if(drawable && winh.findpb(drawable, pbw))
	{
		before=_drawingtofront();
		rbefore=_drawingtoright();
		_glPopAttrib();
		after=_drawingtofront();
		rafter=_drawingtoright();
		if(before && !after) pbw->_dirty=true;
		if(rbefore && !rafter && pbw->stereo()) pbw->_rdirty=true;
	}
	else _glPopAttrib();

		stoptrace();  if(drawable && pbw) {prargi(pbw->_dirty);
		prargi(pbw->_rdirty);  prargx(pbw->getglxdrawable());}  closetrace();

	CATCH();
}

// Sometimes XNextEvent() is called from a thread other than the
// rendering thread, so we wait until glViewport() is called and
// take that opportunity to resize the Pbuffer
void glViewport(GLint x, GLint y, GLsizei width, GLsizei height)
{
	TRY();

	if(ctxh.overlaycurrent()) {_glViewport(x, y, width, height);  return;}

		opentrace(glViewport);  prargi(x);  prargi(y);  prargi(width);
		prargi(height);  starttrace();

	GLXContext ctx=glXGetCurrentContext();
	GLXDrawable draw=_glXGetCurrentDrawable();
	GLXDrawable read=_glXGetCurrentReadDrawable();
	Display *dpy=_glXGetCurrentDisplay();
	GLXDrawable newread=0, newdraw=0;
	if(dpy && (draw || read) && ctx)
	{
		newread=read, newdraw=draw;
		pbwin *drawpbw=NULL, *readpbw=NULL;
		winh.findpb(draw, drawpbw);
		winh.findpb(read, readpbw);
		if(drawpbw) drawpbw->checkresize();
		if(readpbw && readpbw!=drawpbw) readpbw->checkresize();
		if(drawpbw) newdraw=drawpbw->updatedrawable();
		if(readpbw) newread=readpbw->updatedrawable();
		if(newread!=read || newdraw!=draw)
		{
			_glXMakeContextCurrent(dpy, newdraw, newread, ctx);
			if(drawpbw) {drawpbw->clear();  drawpbw->cleanup();}
			if(readpbw) readpbw->cleanup();
		}
	}
	_glViewport(x, y, width, height);

		stoptrace();  if(draw!=newdraw) {prargx(draw);  prargx(newdraw);}
		if(read!=newread) {prargx(read);  prargx(newread);}  closetrace();

	CATCH();
}

// The following nastiness is necessary to make color index rendering work,
// since most platforms don't support color index Pbuffers

#ifdef SUNOGL

static GLvoid r_glIndexd(OglContextPtr ctx, GLdouble c)
{
	TRY();
	if(ctxh.overlaycurrent())
		_throw("r_glIndexd() called on overlay context.");
	else glColor3d(c/255., 0.0, 0.0);
	CATCH();
}

static GLvoid r_glIndexf(OglContextPtr ctx, GLfloat c)
{
	TRY();
	if(ctxh.overlaycurrent())
		_throw("r_glIndexf() called on overlay context.");
	else glColor3f(c/255., 0., 0.);
	CATCH();
}

static GLvoid r_glIndexi(OglContextPtr ctx, GLint c)
{
	TRY();
	if(ctxh.overlaycurrent())
		_throw("r_glIndexi() called on overlay context.");
	else glColor3f((GLfloat)c/255., 0, 0);
	CATCH();
}

static GLvoid r_glIndexs(OglContextPtr ctx, GLshort c)
{
	TRY();
	if(ctxh.overlaycurrent())
		_throw("r_glIndexs() called on overlay context.");
	else glColor3f((GLfloat)c/255., 0, 0);
	CATCH();
}

static GLvoid r_glIndexub(OglContextPtr ctx, GLubyte c)
{
	TRY();
	if(ctxh.overlaycurrent())
		_throw("r_glIndexub() called on overlay context.");
	else glColor3f((GLfloat)c/255., 0, 0);
	CATCH();
}

static GLvoid r_glIndexdv(OglContextPtr ctx, const GLdouble *c)
{
	TRY();
	if(ctxh.overlaycurrent())
		_throw("r_glIndexdv() called on overlay context.");
	else
	{
		GLdouble v[3]={c? (*c)/255.:0., 0., 0.};
		glColor3dv(c? v:NULL);  return;
	}
	CATCH();
}

static GLvoid r_glIndexfv(OglContextPtr ctx, const GLfloat *c)
{
	TRY();
	if(ctxh.overlaycurrent())
		_throw("r_glIndexfv() called on overlay context.");
	else
	{
		GLfloat v[3]={c? (*c)/255.:0., 0., 0.};
		glColor3fv(c? v:NULL);  return;
	}
	CATCH();
}

static GLvoid r_glIndexiv(OglContextPtr ctx, const GLint *c)
{
	TRY();
	if(ctxh.overlaycurrent())
		_throw("r_glIndexiv() called on overlay context.");
	else
	{
		GLfloat v[3]={c? (GLfloat)(*c)/255.:0., 0., 0.};
		glColor3fv(c? v:NULL);  return;
	}
	CATCH();
}

static GLvoid r_glIndexsv(OglContextPtr ctx, const GLshort *c)
{
	TRY();
	if(ctxh.overlaycurrent())
		_throw("r_glIndexsv() called on overlay context.");
	else
	{
		GLfloat v[3]={c? (GLfloat)(*c)/255.:0., 0., 0.};
		glColor3fv(c? v:NULL);  return;
	}
	CATCH();
}

static GLvoid r_glIndexubv(OglContextPtr ctx, const GLubyte *c)
{
	TRY();
	if(ctxh.overlaycurrent())
		_throw("r_glIndexubv() called on overlay context.");
	else
	{
		GLfloat v[3]={c? (GLfloat)(*c)/255.:0., 0., 0.};
		glColor3fv(c? v:NULL);  return;
	}
	CATCH();
}

void glBegin(GLenum mode)
{
	_glBegin(mode);
	if(!ctxh.overlaycurrent())
	{
		sunOglCurPrimTablePtr->oglIndexd=r_glIndexd;
		sunOglCurPrimTablePtr->oglIndexf=r_glIndexf;
		sunOglCurPrimTablePtr->oglIndexi=r_glIndexi;
		sunOglCurPrimTablePtr->oglIndexs=r_glIndexs;
		sunOglCurPrimTablePtr->oglIndexub=r_glIndexub;
		sunOglCurPrimTablePtr->oglIndexdv=r_glIndexdv;
		sunOglCurPrimTablePtr->oglIndexfv=r_glIndexfv;
		sunOglCurPrimTablePtr->oglIndexiv=r_glIndexiv;
		sunOglCurPrimTablePtr->oglIndexsv=r_glIndexsv;
		sunOglCurPrimTablePtr->oglIndexubv=r_glIndexubv;
	}
}

#else

void glIndexd(GLdouble c)
{
	if(ctxh.overlaycurrent()) _glIndexd(c);
	else glColor3d(c/255., 0.0, 0.0);
}

void glIndexf(GLfloat c)
{
	if(ctxh.overlaycurrent()) _glIndexf(c);
	else glColor3f(c/255., 0., 0.);
}

void glIndexi(GLint c)
{
	if(ctxh.overlaycurrent()) _glIndexi(c);
	else glColor3f((GLfloat)c/255., 0, 0);
}

void glIndexs(GLshort c)
{
	if(ctxh.overlaycurrent()) _glIndexs(c);
	else glColor3f((GLfloat)c/255., 0, 0);
}

void glIndexub(GLubyte c)
{
	if(ctxh.overlaycurrent()) _glIndexub(c);
	else glColor3f((GLfloat)c/255., 0, 0);
}

void glIndexdv(const GLdouble *c)
{
	if(ctxh.overlaycurrent()) _glIndexdv(c);
	else
	{
		GLdouble v[3]={c? (*c)/255.:0., 0., 0.};
		glColor3dv(c? v:NULL);
	}
}

void glIndexfv(const GLfloat *c)
{
	if(ctxh.overlaycurrent()) _glIndexfv(c);
	else
	{
		GLfloat v[3]={c? (*c)/255.:0., 0., 0.};
		glColor3fv(c? v:NULL);  return;
	}
}

void glIndexiv(const GLint *c)
{
	if(ctxh.overlaycurrent()) _glIndexiv(c);
	else
	{
		GLfloat v[3]={c? (GLfloat)(*c)/255.:0., 0., 0.};
		glColor3fv(c? v:NULL);  return;
	}
}

void glIndexsv(const GLshort *c)
{
	if(ctxh.overlaycurrent()) _glIndexsv(c);
	else
	{
		GLfloat v[3]={c? (GLfloat)(*c)/255.:0., 0., 0.};
		glColor3fv(c? v:NULL);  return;
	}
}

void glIndexubv(const GLubyte *c)
{
	if(ctxh.overlaycurrent()) _glIndexubv(c);
	else
	{
		GLfloat v[3]={c? (GLfloat)(*c)/255.:0., 0., 0.};
		glColor3fv(c? v:NULL);  return;
	}
}

#endif

void glMaterialfv(GLenum face, GLenum pname, const GLfloat *params)
{
	GLfloat mat[]={1., 1., 1., 1.};
	if(pname==GL_COLOR_INDEXES && params)
	{
		mat[0]=params[0]/255.;
		_glMaterialfv(face, GL_AMBIENT, mat);
		mat[0]=params[1]/255.;
		_glMaterialfv(face, GL_DIFFUSE, mat);
		mat[0]=params[2]/255.;
		_glMaterialfv(face, GL_SPECULAR, mat);
	}
	else _glMaterialfv(face, pname, params);
}

void glMaterialiv(GLenum face, GLenum pname, const GLint *params)
{
	GLfloat mat[]={1., 1., 1., 1.};
	if(pname==GL_COLOR_INDEXES && params)
	{
		mat[0]=(GLfloat)params[0]/255.;
		_glMaterialfv(face, GL_AMBIENT, mat);
		mat[0]=(GLfloat)params[1]/255.;
		_glMaterialfv(face, GL_DIFFUSE, mat);
		mat[0]=(GLfloat)params[2]/255.;
		_glMaterialfv(face, GL_SPECULAR, mat);
	}
	else _glMaterialiv(face, pname, params);
}

#define __round(f) ((f)>=0?(long)((f)+0.5):(long)((f)-0.5))

void glGetDoublev(GLenum pname, GLdouble *params)
{
	if(!ctxh.overlaycurrent())
	{
		if(pname==GL_CURRENT_INDEX)
		{
			GLdouble c[4];
			_glGetDoublev(GL_CURRENT_COLOR, c);
			if(params) *params=(GLdouble)__round(c[0]*255.);
			return;
		}
		else if(pname==GL_CURRENT_RASTER_INDEX)
		{
			GLdouble c[4];
			_glGetDoublev(GL_CURRENT_RASTER_COLOR, c);
			if(params) *params=(GLdouble)__round(c[0]*255.);
			return;
		}
		else if(pname==GL_INDEX_SHIFT)
		{
			_glGetDoublev(GL_RED_SCALE, params);
			if(params) *params=(GLdouble)__round(log(*params)/log(2.));
			return;
		}
		else if(pname==GL_INDEX_OFFSET)
		{
			_glGetDoublev(GL_RED_BIAS, params);
			if(params) *params=(GLdouble)__round((*params)*255.);
			return;
		}
	}
	_glGetDoublev(pname, params);
}

void glGetFloatv(GLenum pname, GLfloat *params)
{
	if(!ctxh.overlaycurrent())
	{
		if(pname==GL_CURRENT_INDEX)
		{
			GLdouble c[4];
			_glGetDoublev(GL_CURRENT_COLOR, c);
			if(params) *params=(GLfloat)__round(c[0]*255.);
			return;
		}
		else if(pname==GL_CURRENT_RASTER_INDEX)
		{
			GLdouble c[4];
			_glGetDoublev(GL_CURRENT_RASTER_COLOR, c);
			if(params) *params=(GLfloat)__round(c[0]*255.);
			return;
		}
		else if(pname==GL_INDEX_SHIFT)
		{
			GLdouble d;
			_glGetDoublev(GL_RED_SCALE, &d);
			if(params) *params=(GLfloat)__round(log(d)/log(2.));
			return;
		}
		else if(pname==GL_INDEX_OFFSET)
		{
			GLdouble d;
			_glGetDoublev(GL_RED_BIAS, &d);
			if(params) *params=(GLfloat)__round(d*255.);
			return;
		}
	}
	_glGetFloatv(pname, params);
}

void glGetIntegerv(GLenum pname, GLint *params)
{
	if(!ctxh.overlaycurrent())
	{
		if(pname==GL_CURRENT_INDEX)
		{
			GLdouble c[4];
			_glGetDoublev(GL_CURRENT_COLOR, c);
			if(params) *params=(GLint)__round(c[0]*255.);
			return;
		}
		else if(pname==GL_CURRENT_RASTER_INDEX)
		{
			GLdouble c[4];
			_glGetDoublev(GL_CURRENT_RASTER_COLOR, c);
			if(params) *params=(GLint)__round(c[0]*255.);
			return;
		}
		else if(pname==GL_INDEX_SHIFT)
		{
			double d;
			_glGetDoublev(GL_RED_SCALE, &d);
			if(params) *params=(GLint)__round(log(d)/log(2.));
			return;
		}
		else if(pname==GL_INDEX_OFFSET)
		{
			double d;
			_glGetDoublev(GL_RED_BIAS, &d);
			if(params) *params=(GLint)__round(d*255.);
			return;
		}
	}
	_glGetIntegerv(pname, params);
}

void glPixelTransferf(GLenum pname, GLfloat param)
{
	if(!ctxh.overlaycurrent())
	{
		if(pname==GL_INDEX_SHIFT)
		{
			_glPixelTransferf(GL_RED_SCALE, pow(2., (double)param));
			return;
		}
		else if(pname==GL_INDEX_OFFSET)
		{
			_glPixelTransferf(GL_RED_BIAS, param/255.);
			return;
		}
	}
	_glPixelTransferf(pname, param);
}

void glPixelTransferi(GLenum pname, GLint param)
{
	if(!ctxh.overlaycurrent())
	{
		if(pname==GL_INDEX_SHIFT)
		{
			_glPixelTransferf(GL_RED_SCALE, pow(2., (double)param));
			return;
		}
		else if(pname==GL_INDEX_OFFSET)
		{
			_glPixelTransferf(GL_RED_BIAS, (GLfloat)param/255.);
			return;
		}
	}
	_glPixelTransferi(pname, param);
}

#define _rpixelconvert(ctype, gltype, size)  \
	if(type==gltype) {  \
		unsigned char *p=(unsigned char *)pixels;  \
		unsigned char *b=buf;  \
		int w=(rowlen>0? rowlen:width)*size;  \
		if(size<align) w=(w+align-1)&(~(align-1));  \
		for(int i=0; i<height; i++, p+=w, b+=width) {  \
			ctype *p2=(ctype *)p;  \
			for(int j=0; j<width; j++) p2[j]=(ctype)b[j];  \
		}}

void glReadPixels(GLint x, GLint y, GLsizei width, GLsizei height,
	GLenum format, GLenum type, GLvoid *pixels)
{
	TRY();
	if(format==GL_COLOR_INDEX && !ctxh.overlaycurrent() && type!=GL_BITMAP)
	{
		format=GL_RED;
		if(type==GL_BYTE || type==GL_UNSIGNED_BYTE) type=GL_UNSIGNED_BYTE;
		else
		{
			int rowlen=-1, align=-1;  GLubyte *buf=NULL;
			_glGetIntegerv(GL_PACK_ALIGNMENT, &align);
			_glGetIntegerv(GL_PACK_ROW_LENGTH, &rowlen);
			newcheck(buf=new unsigned char[width*height])
			if(type==GL_SHORT) type=GL_UNSIGNED_SHORT;
			if(type==GL_INT) type=GL_UNSIGNED_INT;
			glPushClientAttrib(GL_CLIENT_PIXEL_STORE_BIT);
			glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
			glPixelStorei(GL_UNPACK_ROW_LENGTH, 1);
			_glReadPixels(x, y, width, height, format, GL_UNSIGNED_BYTE, buf);
			glPopClientAttrib();
			_rpixelconvert(unsigned short, GL_UNSIGNED_SHORT, 2)
			_rpixelconvert(unsigned int, GL_UNSIGNED_INT, 4)
			_rpixelconvert(float, GL_FLOAT, 4)
			delete [] buf;
			return;
		}
	}
	_glReadPixels(x, y, width, height, format, type, pixels);
	CATCH();
}

#define _dpixelconvert(ctype, gltype, size)  \
	if(type==gltype) {  \
		unsigned char *p=(unsigned char *)pixels;  \
		unsigned char *b=buf;  \
		int w=(rowlen>0? rowlen:width)*size;  \
		if(size<align) w=(w+align-1)&(~(align-1));  \
		for(int i=0; i<height; i++, p+=w, b+=width) {  \
			ctype *p2=(ctype *)p;  \
			for(int j=0; j<width; j++) b[j]=(unsigned char)p2[j];  \
		}}

void glDrawPixels(GLsizei width, GLsizei height, GLenum format, GLenum type,
	const GLvoid *pixels)
{
	TRY();
	if(format==GL_COLOR_INDEX && !ctxh.overlaycurrent() && type!=GL_BITMAP)
	{
		format=GL_RED;
		if(type==GL_BYTE || type==GL_UNSIGNED_BYTE) type=GL_UNSIGNED_BYTE;
		else
		{
			int rowlen=-1, align=-1;  GLubyte *buf=NULL;
			_glGetIntegerv(GL_PACK_ALIGNMENT, &align);
			_glGetIntegerv(GL_PACK_ROW_LENGTH, &rowlen);
			newcheck(buf=new unsigned char[width*height])
			if(type==GL_SHORT) type=GL_UNSIGNED_SHORT;
			if(type==GL_INT) type=GL_UNSIGNED_INT;
			_dpixelconvert(unsigned short, GL_UNSIGNED_SHORT, 2)
			_dpixelconvert(unsigned int, GL_UNSIGNED_INT, 4)
			_dpixelconvert(float, GL_FLOAT, 4)
			glPushClientAttrib(GL_CLIENT_PIXEL_STORE_BIT);
			glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
			glPixelStorei(GL_UNPACK_ROW_LENGTH, 1);
			_glDrawPixels(width, height, format, GL_UNSIGNED_BYTE, buf);
			glPopClientAttrib();
			delete [] buf;
			return;
		}
	}
	_glDrawPixels(width, height, format, type, pixels);
	CATCH();
}

void glClearIndex(GLfloat c)
{
	if(ctxh.overlaycurrent()) _glClearIndex(c);
	else glClearColor(c/255., 0., 0., 0.);
}

} // extern "C"