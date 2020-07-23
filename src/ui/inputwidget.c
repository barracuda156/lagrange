#include "inputwidget.h"
#include "paint.h"
#include "util.h"

#include <the_Foundation/array.h>
#include <SDL_timer.h>

struct Impl_InputWidget {
    iWidget         widget;
    enum iInputMode mode;
    size_t          maxLen;
    iArray          text;    /* iChar[] */
    iArray          oldText; /* iChar[] */
    size_t          cursor;
    int             font;
    iClick          click;
};

iDefineObjectConstructionArgs(InputWidget, (size_t maxLen), maxLen)

void init_InputWidget(iInputWidget *d, size_t maxLen) {
    iWidget *w = &d->widget;
    init_Widget(w);
    setFlags_Widget(w, focusable_WidgetFlag | hover_WidgetFlag, iTrue);
    init_Array(&d->text, sizeof(iChar));
    init_Array(&d->oldText, sizeof(iChar));
    d->font   = uiInput_FontId;
    d->cursor = 0;
    setMaxLen_InputWidget(d, maxLen);
    if (maxLen == 0) {
        /* Caller must arrange the width. */
        w->rect.size.y = lineHeight_Text(d->font) + 2 * gap_UI;
        setFlags_Widget(w, fixedHeight_WidgetFlag, iTrue);
    }
    init_Click(&d->click, d, SDL_BUTTON_LEFT);
}

void deinit_InputWidget(iInputWidget *d) {
    deinit_Array(&d->oldText);
    deinit_Array(&d->text);
}

void setMode_InputWidget(iInputWidget *d, enum iInputMode mode) {
    d->mode = mode;
}

const iString *text_InputWidget(const iInputWidget *d) {
    return collect_String(newUnicodeN_String(constData_Array(&d->text), size_Array(&d->text)));
}

void setMaxLen_InputWidget(iInputWidget *d, size_t maxLen) {
    d->maxLen = maxLen;
    d->mode   = (maxLen == 0 ? insert_InputMode : overwrite_InputMode);
    resize_Array(&d->text, maxLen);
    if (maxLen) {
        /* Set a fixed size. */
        iBlock *content = new_Block(maxLen);
        fill_Block(content, 'M');
        setSize_Widget(
            as_Widget(d),
            add_I2(measure_Text(d->font, cstr_Block(content)), init_I2(6 * gap_UI, 2 * gap_UI)));
        delete_Block(content);
    }
}

void setText_InputWidget(iInputWidget *d, const iString *text) {
    clear_Array(&d->text);
    iConstForEach(String, i, text) {
        pushBack_Array(&d->text, &i.value);
    }
}

void setTextCStr_InputWidget(iInputWidget *d, const char *cstr) {
    iString *str = newCStr_String(cstr);
    setText_InputWidget(d, str);
    delete_String(str);
}

void setCursor_InputWidget(iInputWidget *d, size_t pos) {
    d->cursor = iMin(pos, size_Array(&d->text));
}

void begin_InputWidget(iInputWidget *d) {
    iWidget *w = as_Widget(d);
    if (flags_Widget(w) & selected_WidgetFlag) {
        /* Already active. */
        return;
    }
    setFlags_Widget(w, hidden_WidgetFlag | disabled_WidgetFlag, iFalse);
    setCopy_Array(&d->oldText, &d->text);
    if (d->mode == overwrite_InputMode) {
        d->cursor = 0;
    }
    else {
        d->cursor = iMin(size_Array(&d->text), d->maxLen - 1);
    }
    SDL_StartTextInput();
    setFlags_Widget(w, selected_WidgetFlag, iTrue);
}

void end_InputWidget(iInputWidget *d, iBool accept) {
    iWidget *w = as_Widget(d);
    if (~flags_Widget(w) & selected_WidgetFlag) {
        /* Was not active. */
        return;
    }
    if (!accept) {
        setCopy_Array(&d->text, &d->oldText);
    }
    SDL_StopTextInput();
    setFlags_Widget(w, selected_WidgetFlag, iFalse);
    const char *id = cstr_String(id_Widget(as_Widget(d)));
    if (!*id) id = "_";
    postCommand_Widget(w, "input.ended id:%s arg:%d", id, accept ? 1 : 0);
}

static iBool processEvent_InputWidget_(iInputWidget *d, const SDL_Event *ev) {
    if (isCommand_Widget(as_Widget(d), ev, "focus.gained")) {
        begin_InputWidget(d);
        return iTrue;
    }
    else if (isCommand_Widget(as_Widget(d), ev, "focus.lost")) {
        end_InputWidget(d, iTrue);
        return iTrue;
    }
    switch (processEvent_Click(&d->click, ev)) {
        case none_ClickResult:
            break;
        case started_ClickResult:
        case drag_ClickResult:
        case double_ClickResult:
        case aborted_ClickResult:
            return iTrue;
        case finished_ClickResult:
            setFocus_Widget(as_Widget(d));
            return iTrue;
    }
    if (ev->type == SDL_KEYUP) {
        return iTrue;
    }
    const size_t curMax = iMin(size_Array(&d->text), d->maxLen - 1);
    if (ev->type == SDL_KEYDOWN && isFocused_Widget(as_Widget(d))) {
        const int key  = ev->key.keysym.sym;
        const int mods = keyMods_Sym(ev->key.keysym.mod);
        switch (key) {
            case SDLK_RETURN:
            case SDLK_KP_ENTER:
                setFocus_Widget(NULL);
                return iTrue;
            case SDLK_ESCAPE:
                end_InputWidget(d, iFalse);
                setFocus_Widget(NULL);
                return iTrue;
            case SDLK_BACKSPACE:
                if (mods & KMOD_ALT) {
                    clear_Array(&d->text);
                    d->cursor = 0;
                }
                else if (d->cursor > 0) {
                    remove_Array(&d->text, --d->cursor);
                }
                return iTrue;
            case SDLK_d:
                if (mods != KMOD_CTRL) break;
            case SDLK_DELETE:
                if (d->cursor < size_Array(&d->text)) {
                    remove_Array(&d->text, d->cursor);
                }
                return iTrue;
            case SDLK_k:
                if (mods == KMOD_CTRL) {
                    removeN_Array(&d->text, d->cursor, size_Array(&d->text) - d->cursor);
                    return iTrue;
                }
                break;
            case SDLK_HOME:
            case SDLK_END:
                d->cursor = (key == SDLK_HOME ? 0 : curMax);
                return iTrue;
            case SDLK_a:
            case SDLK_e:
                if (mods == KMOD_CTRL) {
                    d->cursor = (key == 'a' ? 0 : curMax);
                    return iTrue;
                }
                break;
            case SDLK_LEFT:
                if (mods & KMOD_PRIMARY) {
                    d->cursor = 0;
                }
                else if (d->cursor > 0) {
                    d->cursor--;
                }
                return iTrue;
            case SDLK_RIGHT:
                if (mods & KMOD_PRIMARY) {
                    d->cursor = curMax;
                }
                else if (d->cursor < curMax) {
                    d->cursor++;
                }
                return iTrue;
            case SDLK_TAB:
                /* Allow focus switching. */
                return processEvent_Widget(as_Widget(d), ev);
        }
        if (mods & (KMOD_PRIMARY | KMOD_SECONDARY)) {
            return iFalse;
        }
        return iTrue;
    }
    else if (ev->type == SDL_TEXTINPUT && isFocused_Widget(as_Widget(d))) {
        const iString *uni = collectNewCStr_String(ev->text.text);
        const iChar    chr = first_String(uni);
        if (d->mode == insert_InputMode) {
            insert_Array(&d->text, d->cursor, &chr);
            d->cursor++;
        }
        else {
            if (d->cursor >= size_Array(&d->text)) {
                resize_Array(&d->text, d->cursor + 1);
            }
            set_Array(&d->text, d->cursor++, &chr);
            if (d->maxLen && d->cursor == d->maxLen) {
                setFocus_Widget(NULL);
            }
        }
        return iTrue;
    }
    return processEvent_Widget(as_Widget(d), ev);
}

static void draw_InputWidget_(const iInputWidget *d) {
    const uint32_t time   = frameTime_Window(get_Window());
    const iInt2 padding   = init_I2(3 * gap_UI, gap_UI);
    iRect       bounds    = adjusted_Rect(bounds_Widget(constAs_Widget(d)), padding, neg_I2(padding));
    const iBool isFocused = isFocused_Widget(constAs_Widget(d));
    const iBool isHover   = isHover_Widget(constAs_Widget(d)) &&
                            contains_Widget(constAs_Widget(d), mouseCoord_Window(get_Window()));
    iPaint p;
    init_Paint(&p);
    iString text;
    initUnicodeN_String(&text, constData_Array(&d->text), size_Array(&d->text));
    fillRect_Paint(&p, bounds, black_ColorId);
    drawRect_Paint(&p,
                   adjusted_Rect(bounds, neg_I2(one_I2()), zero_I2()),
                   isFocused ? orange_ColorId : isHover ? cyan_ColorId : gray50_ColorId);
    setClip_Paint(&p, bounds);
    const iInt2 emSize    = advance_Text(d->font, "M");
    const int   textWidth = advance_Text(d->font, cstr_String(&text)).x;
    const int   cursorX   = advanceN_Text(d->font, cstr_String(&text), d->cursor).x;
    int         xOff      = 0;
    if (d->maxLen == 0) {
        if (textWidth > width_Rect(bounds) - emSize.x) {
            xOff = width_Rect(bounds) - emSize.x - textWidth;
        }
        if (cursorX + xOff < width_Rect(bounds) / 2) {
            xOff = width_Rect(bounds) / 2 - cursorX;
        }
        xOff = iMin(xOff, 0);
    }
    draw_Text(d->font, addX_I2(topLeft_Rect(bounds), xOff), white_ColorId, cstr_String(&text));
    clearClip_Paint(&p);
    /* Cursor blinking. */
    if (isFocused && (time & 256)) {
        const iInt2 prefixSize = advanceN_Text(d->font, cstr_String(&text), d->cursor);
        const iInt2 curPos     = init_I2(xOff + left_Rect(bounds) + prefixSize.x, top_Rect(bounds));
        const iRect curRect    = { curPos, addX_I2(emSize, 1) };
        iString     cur;
        if (d->cursor < size_Array(&d->text)) {
            initUnicodeN_String(&cur, constAt_Array(&d->text, d->cursor), 1);
        }
        else {
            initCStr_String(&cur, " ");
        }
        fillRect_Paint(&p, curRect, orange_ColorId);
        draw_Text(d->font, curPos, black_ColorId, cstr_String(&cur));
        deinit_String(&cur);
    }
    deinit_String(&text);
}

iBeginDefineSubclass(InputWidget, Widget)
    .processEvent = (iAny *) processEvent_InputWidget_,
    .draw         = (iAny *) draw_InputWidget_,
iEndDefineSubclass(InputWidget)
