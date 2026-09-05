#ifndef _GUI_H_
#define _GUI_H_

#ifdef __cplusplus
extern "C" {
#endif
	void gui_init();
	/* El evento va como puntero opaco: gui.h no incluye SDL para que main.h
	   tampoco tenga que hacerlo. Dentro es un SDL_Event. */
	void gui_event(const void * evt);
	void gui_refresh();
	void gui_addlog(char * str);
	void gui_addlogchar(char c);
	void gui_setvisiblelog(bool vis);
	bool gui_isvisiblelog();
#ifdef __cplusplus
};
#endif

#endif // _GUI_H_
