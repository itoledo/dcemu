// La ventana de log usa guichan, que no tiene releases desde 2010 y no compila
// con MSVC. Queda detras de USE_GUICHAN: sin ese define se compilan stubs vacios
// y el emulador funciona igual, solo sin la ventana de log (F2).

#include <iostream>
#include <SDL/SDL.h>

#ifdef USE_GUICHAN
#include <guichan.hpp>
#include <guichan/sdl.hpp>
#include <guichan/opengl.hpp>
#include <guichan/opengl/openglsdlimageloader.hpp>
#endif

#include "gui.h"

#ifdef WIN32
#include <windows.h>
#endif

#include <GL/gl.h>

#ifdef USE_GUICHAN

gcn::Gui* gui;            // A Gui object - binds it all together
gcn::ImageFont* font;     // A font
gcn::SDLInput* input;                 // Input driver
gcn::OpenGLGraphics* graphics;        // Graphics driver
gcn::OpenGLSDLImageLoader* imageLoader;  // For loading images
gcn::Container* top;                 // A top container
gcn::Window *window;
gcn::Icon* debugIcon; 
gcn::ScrollArea * scrollArea;
gcn::TextBox * textBox;
// gcn::Label * label;

extern "C" {

void gui_init()
{
	try
	{
		// inicializar estructuras
		imageLoader = new gcn::OpenGLSDLImageLoader();
		gcn::Image::setImageLoader(imageLoader); 

		graphics = new gcn::OpenGLGraphics();
		graphics->setTargetPlane(800, 600);
		input = new gcn::SDLInput();
		top = new gcn::Container();    
		top->setDimension(gcn::Rectangle(0, 0, 800, 600));
		top->setOpaque(false);
		top->setBaseColor(gcn::Color(0x33, 0x10, 0x10, 128));
		gui = new gcn::Gui();
		gui->setGraphics(graphics);
		gui->setInput(input);	
		gui->setTop(top);
		font = new gcn::ImageFont("fixedfont.bmp", " abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789");
		gcn::Widget::setGlobalFont(font);
	
		window = new gcn::Window("Log Window");
		window->setBaseColor(gcn::Color(0x33, 0x10, 0x10, 128));
		window->setOpaque(false);
//		window->setBackgroundColor(gcn::Color(255, 255, 255, 255));

		textBox = new gcn::TextBox();
		textBox->setBaseColor(gcn::Color(0x33, 0x10, 0x10, 128));
		textBox->setOpaque(false);
		
		scrollArea = new gcn::ScrollArea(textBox);
		scrollArea->setWidth(400);
		scrollArea->setHeight(200);
		scrollArea->setBorderSize(1);
		scrollArea->setScrollPolicy(gcn::ScrollArea::SHOW_AUTO, gcn::ScrollArea::SHOW_AUTO);
		scrollArea->setBaseColor(gcn::Color(0x33, 0x10, 0x10, 128));
		scrollArea->setBackgroundColor(0x331010);
		scrollArea->setForegroundColor(0x331010);
//		scrollArea->setOpaque(false);
		
		window->add(scrollArea);
		window->resizeToContent();
		top->add(window, 40, 40);
	}
	catch (...)
	{
		fprintf(stderr, "inicializar_gui: excepcion\n");
	}
}

void gui_event(SDL_Event evt)
{
	input->pushInput(evt);
}

void gui_refresh()
{
	gui->logic();
	glDisable(GL_TEXTURE_2D);
//	glDisable(GL_DEPTH_TEST);
//	glPushAttrib(GL_TEXTURE_BIT);
	gui->draw();
//	glPopAttrib();
	glEnable(GL_TEXTURE_2D);
//	glEnable(GL_DEPTH_TEST);
}

void gui_addlog(char * str)
{
	if (textBox)
		textBox->addRow(str);
}

void gui_addlogchar(char c)
{
#define MAXBUF (256)
	static char buf[MAXBUF];
	static int i = 0;
	
	buf[i++] = c;
	if (c == '\n' || i == MAXBUF - 1)
	{
		buf[i] = '\0';
		gui_addlog(buf);
		i = 0;
	}
}

void gui_setvisiblelog(bool vis)
{
	window->setVisible(vis);
}

bool gui_isvisiblelog()
{
	return window->isVisible();
}

};

#else // !USE_GUICHAN

extern "C" {

void gui_init() {}
void gui_event(SDL_Event evt) { (void) evt; }
void gui_refresh() {}
void gui_addlog(char * str) { (void) str; }
void gui_addlogchar(char c) { (void) c; }
void gui_setvisiblelog(bool vis) { (void) vis; }
bool gui_isvisiblelog() { return false; }

};

#endif // USE_GUICHAN
