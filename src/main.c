/*
 * main.c — cterm with full GRiD OS retro theme.
 *
 * Terminal text colors override:
 *   Default FG → yellow-green phosphor (RT_TEXT)
 *   Default BG → near-black (RT_BG)
 *   Cursor     → warm green (RT_CURSOR)
 *   Selection  → bright green bg, black text (RT_SEL)
 *   Pane dividers → green border lines
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL2/SDL.h>
#include <sys/wait.h>
#include "window.h"
#include "font.h"
#include "tabs.h"
#include "pane.h"
#include "ansi.h"
#include "tools.h"
#include "config.h"
#include "retro_theme.h"

#define TARGET_FPS 60
#define FRAME_MS   (1000 / TARGET_FPS)

/* ════════════════════════════════════════════════════════
 * Selection helpers
 * ════════════════════════════════════════════════════════ */
static void pixel_to_cell(int px, int py, SDL_Rect r,
                           int cw, int ch, int *col, int *row) {
    *col = (px - r.x) / cw;
    *row = (py - r.y) / ch;
    if (*col < 0) *col = 0;
    if (*row < 0) *row = 0;
}

static char *build_selection_text(Terminal *term, Selection *sel,
                                   int cw, int ch) {
    (void)cw; (void)ch;
    if (!sel->has_selection) return NULL;
    int r1=sel->start_row, c1=sel->start_col;
    int r2=sel->end_row,   c2=sel->end_col;
    if (r1>r2||(r1==r2&&c1>c2)) {
        int t; t=r1;r1=r2;r2=t; t=c1;c1=c2;c2=t;
    }
    if (r1<0) r1=0; if (r2>=term->rows) r2=term->rows-1;
    if (c1<0) c1=0; if (c2>=term->cols) c2=term->cols-1;
    char *buf = malloc((r2-r1+1)*(term->cols+2)+1);
    if (!buf) return NULL;
    int pos=0;
    for (int row=r1; row<=r2; row++) {
        int cs=(row==r1)?c1:0, ce=(row==r2)?c2:term->cols-1;
        int last=-1;
        for (int c=cs;c<=ce&&c<term->cols;c++)
            if(term->cells[row][c].ch!=' '&&term->cells[row][c].ch!='\0')
                last=c;
        for (int c=cs;c<=last&&c<term->cols;c++) {
            char ch2=term->cells[row][c].ch;
            if (!ch2) ch2=' ';
            buf[pos++]=ch2;
        }
        if (row<r2) buf[pos++]='\n';
    }
    buf[pos]='\0';
    return buf;
}

static int cell_in_selection(Selection *sel, int row, int col) {
    if (!sel->has_selection && !sel->active) return 0;
    int r1=sel->start_row,c1=sel->start_col;
    int r2=sel->end_row,  c2=sel->end_col;
    if (r1>r2||(r1==r2&&c1>c2)) {
        int t; t=r1;r1=r2;r2=t; t=c1;c1=c2;c2=t;
    }
    if (row<r1||row>r2) return 0;
    if (row==r1&&col<c1) return 0;
    if (row==r2&&col>c2) return 0;
    return 1;
}

/* ════════════════════════════════════════════════════════
 * Exit detection — fixes "exit" command
 * ════════════════════════════════════════════════════════ */
static int check_shell_exited(Pane *p) {
    if (!p||p->type!=PANE_LEAF||p->pty.shell_pid<=0) return 0;
    int status;
    pid_t r = waitpid(p->pty.shell_pid, &status, WNOHANG);
    if (r==p->pty.shell_pid) { p->pty.shell_pid=-1; return 1; }
    return 0;
}

static int check_all_panes_exit(TabManager *tm) {
    int closed=0;
    for (int i=0; i<tm->count; i++) {
        Tab *tab=&tm->tabs[i];
        if (!tab->alive||!tab->root) continue;
        Pane *stk[128]; int sp=0;
        stk[sp++]=tab->root;
        Pane *leaves[64]; int lc=0;
        while (sp>0) {
            Pane *p=stk[--sp]; if (!p) continue;
            if (p->type==PANE_LEAF) { if(lc<64) leaves[lc++]=p; }
            else { if(p->second) stk[sp++]=p->second;
                   if(p->first)  stk[sp++]=p->first; }
        }
        for (int j=0; j<lc; j++) {
            if (check_shell_exited(leaves[j])) {
                pane_set_focus(tab->root, leaves[j]);
                tab->root = pane_close_focused(tab->root);
                closed=1;
                if (!tab->root) {
                    tabs_close(tm, i); i--; break;
                }
            }
        }
    }
    return closed;
}

/* ════════════════════════════════════════════════════════
 * Render pane tree — retro phosphor colors
 * ════════════════════════════════════════════════════════ */
static void render_pane_tree(Pane *p, SDL_Renderer *renderer,
                              Font *font, Selection *sel) {
    if (!p) return;
    if (p->type != PANE_LEAF) {
        render_pane_tree(p->first,  renderer, font, sel);
        render_pane_tree(p->second, renderer, font, sel);

        /* Draw pane divider — retro green border line */
        SDL_SetRenderDrawColor(renderer,
                               RT_BORDER_R, RT_BORDER_G, RT_BORDER_B, 255);
        if (p->type == PANE_SPLIT_H) {
            int mx = p->rect.x + (int)(p->rect.w * p->ratio);
            SDL_RenderDrawLine(renderer, mx, p->rect.y,
                               mx, p->rect.y + p->rect.h);
        } else {
            int my = p->rect.y + (int)(p->rect.h * p->ratio);
            SDL_RenderDrawLine(renderer, p->rect.x, my,
                               p->rect.x + p->rect.w, my);
        }
        return;
    }

    Terminal *term = p->term;
    SDL_Rect   r   = p->rect;
    if (r.w < font->cell_width || r.h < font->cell_height) return;

    int pcols = r.w / font->cell_width;
    int prows = r.h / font->cell_height;
    if (pcols<1) pcols=1; if (prows<1) prows=1;
    if (pcols!=term->cols||prows!=term->rows) {
        terminal_resize(term,pcols,prows);
        pty_resize(&p->pty,pcols,prows);
    }

    int force = (term->scroll_offset > 0);

    for (int row=0; row<term->rows; row++) {
        Cell *dr = terminal_get_display_row(term, row);
        for (int col=0; col<term->cols; col++) {
            int x = r.x + col*font->cell_width;
            int y = r.y + row*font->cell_height;
            if (x+font->cell_width >r.x+r.w) continue;
            if (y+font->cell_height>r.y+r.h) continue;
            if (!force && dr && !dr[col].dirty) continue;

            /*
             * Color strategy:
             * If cell has NON-DEFAULT colors (colored prompt, syntax hl)
             * → use those exact ANSI colors
             * If cell is default fg/bg → apply retro phosphor palette
             */
            Uint8 bg_r, bg_g, bg_b, fg_r, fg_g, fg_b;
            char  ch = ' ';
            int   is_default_bg = 1, is_default_fg = 1;

            if (dr) {
                ch   = dr[col].ch;
                fg_r = dr[col].fg.r; fg_g = dr[col].fg.g; fg_b = dr[col].fg.b;
                bg_r = dr[col].bg.r; bg_g = dr[col].bg.g; bg_b = dr[col].bg.b;

                /* Detect default colors (200,200,200 fg / 0,0,0 bg) */
                is_default_fg = (fg_r==200&&fg_g==200&&fg_b==200);
                is_default_bg = (bg_r==0  &&bg_g==0  &&bg_b==0);

                /* Apply retro phosphor for default colors */
                if (is_default_fg) { fg_r=RT_TEXT_R; fg_g=RT_TEXT_G; fg_b=RT_TEXT_B; }
                if (is_default_bg) { bg_r=RT_BG_R;   bg_g=RT_BG_G;   bg_b=RT_BG_B;   }

                if (dr==term->cells[row]) term->cells[row][col].dirty=0;
            } else {
                bg_r=RT_BG_R; bg_g=RT_BG_G; bg_b=RT_BG_B;
                fg_r=RT_TEXT_R; fg_g=RT_TEXT_G; fg_b=RT_TEXT_B;
            }

            /* Selection highlight — bright green bg, black text */
            int selected = p->focused && cell_in_selection(sel, row, col);
            if (selected) {
                bg_r=RT_SEL_BG_R; bg_g=RT_SEL_BG_G; bg_b=RT_SEL_BG_B;
                fg_r=RT_SEL_FG_R; fg_g=RT_SEL_FG_G; fg_b=RT_SEL_FG_B;
            }

            SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
            SDL_SetRenderDrawColor(renderer, bg_r, bg_g, bg_b, 255);
            SDL_Rect bg_rect={x,y,font->cell_width,font->cell_height};
            SDL_RenderFillRect(renderer,&bg_rect);

            if (ch!=' '&&ch!='\0')
                font_draw_char(font,renderer,ch,x,y,fg_r,fg_g,fg_b);
        }
    }

    /* Blinking block cursor — phosphor green */
    if (p->focused && term->scroll_offset==0) {
        Uint32 ticks=SDL_GetTicks();
        if ((ticks/500)%2==0) {
            SDL_SetRenderDrawColor(renderer,
                                   RT_CURSOR_R,RT_CURSOR_G,RT_CURSOR_B,220);
            SDL_Rect cur={
                r.x+term->cursor_col*font->cell_width,
                r.y+term->cursor_row*font->cell_height,
                font->cell_width, font->cell_height
            };
            SDL_RenderFillRect(renderer,&cur);
        }
    }

    /* Scroll indicator */
    if (p->focused && term->scroll_offset>0) {
        SDL_SetRenderDrawColor(renderer,
                               RT_BORDER_R,RT_BORDER_G,RT_BORDER_B,220);
        SDL_Rect tl={r.x,r.y,r.w,2};
        SDL_RenderFillRect(renderer,&tl);
        if (term->sb_count>0) {
            float ratio=(float)term->scroll_offset/(float)term->sb_count;
            int bh=r.h/8, by=r.y+(int)((r.h-bh)*(1.0f-ratio));
            SDL_SetRenderDrawColor(renderer,
                                   RT_SEL_BG_R,RT_SEL_BG_G,RT_SEL_BG_B,160);
            SDL_Rect bar={r.x+r.w-4,by,4,bh};
            SDL_RenderFillRect(renderer,&bar);
        }
    }

    /* Focused pane border — thin green outline */
    if (p->focused) {
        SDL_SetRenderDrawColor(renderer,
                               RT_BORDER_R,RT_BORDER_G,RT_BORDER_B,120);
        SDL_RenderDrawRect(renderer,&r);
    }
}

/* ════════════════════════════════════════════════════════
 * main
 * ════════════════════════════════════════════════════════ */
int main(void) {
    config_load();

    Window win; Font font; TabManager tm; ToolManager tools;

    if (window_init(&win,g_config.window_width,g_config.window_height)!=0)
        return 1;
    if (g_config.start_fullscreen) window_toggle_fullscreen(&win);

    if (font_init(&font,win.renderer,g_config.font_path,g_config.font_size)!=0)
        if (font_init(&font,win.renderer,"../assets/font.ttf",g_config.font_size)!=0)
            { window_destroy(&win); return 1; }

    int cols=win.width/font.cell_width;
    int rows=(win.height-TAB_BAR_HEIGHT)/font.cell_height;
    if (cols<1) cols=1; if (rows<1) rows=1;

    if (tabs_init(&tm,cols,rows)!=0)
        { font_destroy(&font); window_destroy(&win); return 1; }

    tools_init(&tools);

    char  read_buf[4096];
    int   running=1;
    Uint32 frame_start;
    Pane  *sel_pane=NULL;

    while (running) {
        frame_start=SDL_GetTicks();
        Tab *tab=tabs_get_active(&tm);

        /* ── Events ── */
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {

            if (ev.type==SDL_QUIT) { running=0; continue; }

            if (ev.type==SDL_WINDOWEVENT &&
                (ev.window.event==SDL_WINDOWEVENT_RESIZED||
                 ev.window.event==SDL_WINDOWEVENT_SIZE_CHANGED)) {
                SDL_GetWindowSize(win.window,&win.width,&win.height);
                cols=win.width/font.cell_width;
                rows=(win.height-TAB_BAR_HEIGHT)/font.cell_height;
                if(cols<1)cols=1; if(rows<1)rows=1;
                continue;
            }

            /* Mouse down */
            if (ev.type==SDL_MOUSEBUTTONDOWN) {
                int mx=ev.button.x, my=ev.button.y;
                if (ev.button.button==SDL_BUTTON_LEFT) {
                    if (tools.launcher.visible) {
                        tools_launcher_close(&tools); continue;
                    }
                    if (my<TAB_BAR_HEIGHT) {
                        if (tabs_handle_click(&tm,mx,my,cols,rows))
                            tab=tabs_get_active(&tm);
                    } else {
                        Pane *clicked=pane_find_at(tab->root,mx,my);
                        if (clicked) {
                            pane_set_focus(tab->root,clicked);
                            sel_pane=clicked;
                            win.sel.active=1; win.sel.has_selection=0;
                            int c2,r2;
                            pixel_to_cell(mx,my,clicked->rect,
                                          font.cell_width,font.cell_height,
                                          &c2,&r2);
                            win.sel.start_col=c2; win.sel.start_row=r2;
                            win.sel.end_col  =c2; win.sel.end_row  =r2;
                        }
                    }
                }
                if (ev.button.button==SDL_BUTTON_RIGHT) {
                    Pane *fp=pane_get_focused(tab->root);
                    if (fp&&my>=TAB_BAR_HEIGHT) {
                        char *clip=clipboard_paste();
                        if (clip) { pty_write(&fp->pty,clip,(int)strlen(clip)); SDL_free(clip); }
                    }
                }
                continue;
            }

            /* Mouse up — finalize selection */
            if (ev.type==SDL_MOUSEBUTTONUP&&ev.button.button==SDL_BUTTON_LEFT) {
                if (win.sel.active) {
                    win.sel.active=0;
                    if (win.sel.start_row!=win.sel.end_row||
                        win.sel.start_col!=win.sel.end_col) {
                        win.sel.has_selection=1;
                        if (sel_pane&&sel_pane->term) {
                            char *txt=build_selection_text(sel_pane->term,&win.sel,
                                                           font.cell_width,font.cell_height);
                            if(txt) { clipboard_copy(txt); free(txt); }
                        }
                    } else { win.sel.has_selection=0; }
                }
                continue;
            }

            /* Mouse motion — update selection */
            if (ev.type==SDL_MOUSEMOTION&&win.sel.active) {
                int mx=ev.motion.x,my=ev.motion.y;
                if (sel_pane) {
                    int c2,r2;
                    pixel_to_cell(mx,my,sel_pane->rect,
                                  font.cell_width,font.cell_height,&c2,&r2);
                    win.sel.end_col=c2; win.sel.end_row=r2;
                    if(sel_pane->term)
                        for(int rr=0;rr<sel_pane->term->rows;rr++)
                            for(int cc=0;cc<sel_pane->term->cols;cc++)
                                sel_pane->term->cells[rr][cc].dirty=1;
                }
                continue;
            }

            /* Mouse wheel */
            if (ev.type==SDL_MOUSEWHEEL&&!tools.launcher.visible) {
                Pane *fp=pane_get_focused(tab->root);
                if(fp&&fp->term) {
                    if(ev.wheel.y>0) {
                        fp->term->scroll_offset+=3;
                        if(fp->term->scroll_offset>fp->term->sb_count)
                            fp->term->scroll_offset=fp->term->sb_count;
                    } else if(ev.wheel.y<0) {
                        fp->term->scroll_offset-=3;
                        if(fp->term->scroll_offset<0)
                            fp->term->scroll_offset=0;
                    }
                }
                continue;
            }

            /* Text input */
            if (ev.type==SDL_TEXTINPUT) {
                if (tools.launcher.visible)
                    tools_launcher_handle_text(&tools,ev.text.text);
                else {
                    Pane *fp=pane_get_focused(tab->root);
                    if(fp) {
                        fp->term->scroll_offset=0;
                        win.sel.has_selection=0;
                        pty_write(&fp->pty,ev.text.text,strlen(ev.text.text));
                    }
                }
                continue;
            }

            /* Key down */
            if (ev.type==SDL_KEYDOWN) {
                SDL_Keycode sym=ev.key.keysym.sym;
                SDL_Keymod  mod=SDL_GetModState();
                int ctrl =(mod&KMOD_CTRL)!=0;
                int shift=(mod&KMOD_SHIFT)!=0;
                int alt  =(mod&KMOD_ALT)!=0;

                if (tools.launcher.visible) {
                    tools_launcher_handle_key(&tools,sym,mod,&tm,cols,rows);
                    tab=tabs_get_active(&tm); continue;
                }

                if (sym==SDLK_F11||(alt&&sym==SDLK_RETURN)) {
                    window_toggle_fullscreen(&win);
                    SDL_GetWindowSize(win.window,&win.width,&win.height);
                    cols=win.width/font.cell_width;
                    rows=(win.height-TAB_BAR_HEIGHT)/font.cell_height;
                    continue;
                }
                if (ctrl&&shift&&sym==SDLK_c) {
                    Pane *fp=pane_get_focused(tab->root);
                    if(fp&&fp->term&&win.sel.has_selection) {
                        char *txt=build_selection_text(fp->term,&win.sel,
                                   font.cell_width,font.cell_height);
                        if(txt) { clipboard_copy(txt); free(txt); }
                    }
                    continue;
                }
                if (ctrl&&shift&&sym==SDLK_v) {
                    Pane *fp=pane_get_focused(tab->root);
                    if(fp) {
                        char *clip=clipboard_paste();
                        if(clip) { pty_write(&fp->pty,clip,(int)strlen(clip)); SDL_free(clip); }
                    }
                    continue;
                }
                if (ctrl&&!shift&&sym==SDLK_t) {
                    tabs_new(&tm,cols,rows); tab=tabs_get_active(&tm); continue;
                }
                if (ctrl&&!shift&&sym==SDLK_w) {
                    tabs_close(&tm,tm.active); tab=tabs_get_active(&tm); continue;
                }
                if (ctrl&&!shift&&sym==SDLK_TAB) {
                    tabs_next(&tm); tab=tabs_get_active(&tm); continue;
                }
                if (ctrl&&shift&&sym==SDLK_TAB) {
                    tabs_prev(&tm); tab=tabs_get_active(&tm); continue;
                }
                if (ctrl&&!shift&&sym>=SDLK_1&&sym<=SDLK_9) {
                    tabs_set_active(&tm,sym-SDLK_1); tab=tabs_get_active(&tm); continue;
                }
                if (ctrl&&!shift&&sym==SDLK_p) {
                    tools_launcher_open(&tools); continue;
                }
                if (ctrl&&shift&&sym==SDLK_RIGHT) {
                    Pane *fp=pane_get_focused(tab->root);
                    if(fp) { Pane *nw=pane_split(fp,PANE_SPLIT_H,cols,rows);
                             if(nw!=fp) tab->root=nw; }
                    continue;
                }
                if (ctrl&&shift&&sym==SDLK_DOWN) {
                    Pane *fp=pane_get_focused(tab->root);
                    if(fp) { Pane *nw=pane_split(fp,PANE_SPLIT_V,cols,rows);
                             if(nw!=fp) tab->root=nw; }
                    continue;
                }
                if (ctrl&&shift&&sym==SDLK_w) {
                    tab->root=pane_close_focused(tab->root);
                    if(!tab->root) { tabs_close(&tm,tm.active); tab=tabs_get_active(&tm); }
                    continue;
                }
                if (ctrl&&shift&&sym==SDLK_f) {
                    pane_focus_next(tab->root); continue;
                }
                if (ctrl&&sym>=SDLK_a&&sym<=SDLK_z) {
                    Pane *fp=pane_get_focused(tab->root);
                    if(fp) { char cc=(char)(sym-SDLK_a+1); pty_write(&fp->pty,&cc,1); }
                    continue;
                }
                {
                    Pane *fp=pane_get_focused(tab->root);
                    if(!fp) continue;
                    PTY *pty=&fp->pty; Terminal *tfp=fp->term;
                    switch(sym) {
                        case SDLK_RETURN:
                            tfp->scroll_offset=0; win.sel.has_selection=0;
                            pty_write(pty,"\r",1); break;
                        case SDLK_BACKSPACE: pty_write(pty,"\x7f",1); break;
                        case SDLK_TAB:       pty_write(pty,"\t",1);   break;
                        case SDLK_ESCAPE:    pty_write(pty,"\x1b",1); break;
                        case SDLK_UP:        pty_write(pty,"\x1b[A",3); break;
                        case SDLK_DOWN:      pty_write(pty,"\x1b[B",3); break;
                        case SDLK_RIGHT:     pty_write(pty,"\x1b[C",3); break;
                        case SDLK_LEFT:      pty_write(pty,"\x1b[D",3); break;
                        case SDLK_HOME:      pty_write(pty,"\x1b[H",3); break;
                        case SDLK_END:       pty_write(pty,"\x1b[F",3); break;
                        case SDLK_DELETE:    pty_write(pty,"\x1b[3~",4); break;
                        case SDLK_PAGEUP:
                            tfp->scroll_offset+=rows/2;
                            if(tfp->scroll_offset>tfp->sb_count)
                                tfp->scroll_offset=tfp->sb_count;
                            break;
                        case SDLK_PAGEDOWN:
                            tfp->scroll_offset-=rows/2;
                            if(tfp->scroll_offset<0) tfp->scroll_offset=0;
                            break;
                        default: break;
                    }
                }
                continue;
            }
        } /* end PollEvent */

        /* ── Read PTY ── */
        for (int i=0; i<tm.count; i++) {
            Tab *t=&tm.tabs[i]; if(!t->alive||!t->root) continue;
            Pane *stk[64]; int sp=0; stk[sp++]=t->root;
            while(sp>0) {
                Pane *p=stk[--sp]; if(!p) continue;
                if(p->type==PANE_LEAF) {
                    int n=pty_read(&p->pty,read_buf,sizeof(read_buf));
                    if(n>0) {
                        terminal_process(p->term,read_buf,n);
                        if(i!=tm.active) t->has_activity=1;
                    }
                } else {
                    if(p->second) stk[sp++]=p->second;
                    if(p->first)  stk[sp++]=p->first;
                }
            }
        }

        /* ── Exit detection ── */
        if (check_all_panes_exit(&tm)) {
            tab=tabs_get_active(&tm);
            if(tm.count==0) { running=0; continue; }
        }

        /* ── Render ── */
        window_render_begin(&win);

        tabs_draw_bar(&tm,win.renderer,&font,win.width);

        tab=tabs_get_active(&tm);
        SDL_Rect term_area={0,TAB_BAR_HEIGHT,
                            win.width,win.height-TAB_BAR_HEIGHT};
        pane_layout(tab->root,term_area);
        render_pane_tree(tab->root,win.renderer,&font,&win.sel);

        if (tools.launcher.visible)
            tools_launcher_draw(&tools,win.renderer,
                                &font,win.width,win.height);

        window_render_end(&win);

        /* 60fps cap */
        Uint32 ft=SDL_GetTicks()-frame_start;
        if(ft<FRAME_MS) SDL_Delay(FRAME_MS-ft);

    } /* end main loop */

    tabs_destroy(&tm);
    font_destroy(&font);
    window_destroy(&win);
    return 0;
}