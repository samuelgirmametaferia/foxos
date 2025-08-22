#include <stdint.h>
#include "console.h"
#include "vfs.h"
#include "render.h"
#include "window.h"

// Simple parser helpers
static int isspace_c(char c){ return c==' '||c=='\t'||c=='\r'||c=='\n'; }
static int streq(const char* a,const char* b){ while(*a&&*b){ if(*a!=*b) return 0; ++a; ++b;} return *a==0&&*b==0; }
static int startswith(const char* s,const char* p){ while(*p){ if(*s++!=*p++) return 0; } return 1; }
static char tolower_c(char c){ return (c>='A'&&c<='Z')?(char)(c+32):c; }

static uint8_t color_from_name(const char* s){
    // simple VGA 0..15 mapping by name or number
    if (!s||!*s) return 7;
    if (startswith(s,"black")) return 0;
    if (startswith(s,"blue")) return 1;
    if (startswith(s,"green")) return 2;
    if (startswith(s,"cyan")) return 3;
    if (startswith(s,"red")) return 4;
    if (startswith(s,"magenta")) return 5;
    if (startswith(s,"brown")||startswith(s,"yellow")) return 6;
    if (startswith(s,"lightgray")||startswith(s,"grey")) return 7;
    if (startswith(s,"darkgray")) return 8;
    if (startswith(s,"lightblue")) return 9;
    if (startswith(s,"lightgreen")) return 10;
    if (startswith(s,"lightcyan")) return 11;
    if (startswith(s,"lightred")) return 12;
    if (startswith(s,"lightmagenta")) return 13;
    if (startswith(s,"lightyellow")) return 14;
    if (startswith(s,"white")) return 15;
    // numeric 0..15
    uint32_t v=0; for (int i=0;s[i];++i){ if (s[i]<'0'||s[i]>'9') return 7; v = v*10 + (uint32_t)(s[i]-'0'); if (v>15) return 7; }
    return (uint8_t)v;
}

static void trim(char* s){ int i=0,j=0; while(s[i]&&isspace_c(s[i])) i++; while(s[i]) s[j++]=s[i++]; s[j]=0; while(j>0 && isspace_c(s[j-1])) s[--j]=0; }

static void write_text(const char* s){ for(int i=0; s[i]; ++i){ console_putc(s[i]); } }

// Very tiny style parser: style="color: X; background: Y"
typedef struct { uint8_t fg; uint8_t bg; } style_t;
static style_t parse_style(const char* s, style_t base){
    style_t st = base;
    if(!s) return st;
    char buf[128]; int bi=0;
    for(int i=0; s[i] && bi<(int)sizeof(buf)-1; ++i){ char c=s[i]; buf[bi++]=tolower_c(c); }
    buf[bi]=0;
    // find color
    const char* p=buf;
    while(*p){
        while(*p && isspace_c(*p)) p++;
        if(startswith(p,"color:")){
            p+=6; while(*p==':'||isspace_c(*p)) p++; const char* v=p; while(*p && *p!=';' ) p++; char save=*p; *((char*)p)=0; st.fg=color_from_name(v); *((char*)p)=save; if(*p==';') p++; continue;
        }
        if(startswith(p,"background:")){
            p+=10; while(*p==':'||isspace_c(*p)) p++; const char* v=p; while(*p && *p!=';' ) p++; char save=*p; *((char*)p)=0; st.bg=color_from_name(v); *((char*)p)=save; if(*p==';') p++; continue;
        }
        while(*p && *p!=';') p++; if(*p==';') p++;
    }
    return st;
}

int render_file(const char* path){
    char buf[2048];
    uint32_t n=0; if (vfs_read(path, buf, sizeof(buf)-1, &n)!=0){ console_writeln("render: file not found"); return -1; }
    buf[n]=0;

    // Clear and draw a window for rendering
    console_clear();
    Window win; window_init(&win, 4, 2, 72, 20, "Renderer", 15, 0);
    window_draw(&win);
    window_clear_client(&win);

    // set starting color
    uint8_t cur_fg = 15, cur_bg = 0;

    // scan through very simply
    for (int i=0; buf[i]; ){
        if (buf[i] == '<'){
            int j=i+1; while(buf[j] && buf[j] != '>') j++;
            if(!buf[j]) break; // malformed
            char tag[128]; int tlen= j-(i+1); if(tlen>120) tlen=120; int k=0; for(;k<tlen;++k) tag[k]=buf[i+1+k]; tag[k]=0;
            for(int q=0; tag[q]; ++q) tag[q]=tolower_c(tag[q]);

            if (startswith(tag,"br")) {
                window_putc(&win,'\n');
            } else if (startswith(tag,"h1")) {
                // style
                const char* sattr = 0; char* a = tag; while(*a){ if(startswith(a,"style=")){ sattr=a+6; break; } a++; }
                if(sattr && (*sattr=='"' || *sattr=='\'')) { char qch=*sattr++; char tmp[96]; int ti=0; while(*sattr && *sattr!=qch && ti<95) tmp[ti++]=*sattr++; tmp[ti]=0; style_t st = parse_style(tmp,(style_t){cur_fg,cur_bg}); cur_fg=st.fg; cur_bg=st.bg; }
                window_write(&win, "# ");
                int p=j+1; while(buf[p] && buf[p]!='<') p++; char txt[256]; int ti=0; for(int z=j+1; z<p && ti<255; ++z) txt[ti++]=buf[z]; txt[ti]=0; trim(txt); window_writeln(&win, txt);
            } else if (startswith(tag,"p")) {
                const char* sattr = 0; char* a = tag; while(*a){ if(startswith(a,"style=")){ sattr=a+6; break; } a++; }
                if(sattr && (*sattr=='"' || *sattr=='\'')) { char qch=*sattr++; char tmp[96]; int ti=0; while(*sattr && *sattr!=qch && ti<95) tmp[ti++]=*sattr++; tmp[ti]=0; style_t st = parse_style(tmp,(style_t){cur_fg,cur_bg}); cur_fg=st.fg; cur_bg=st.bg; }
                int p=j+1; while(buf[p] && buf[p]!='<') p++; char txt[512]; int ti=0; for(int z=j+1; z<p && ti<511; ++z) txt[ti++]=buf[z]; txt[ti]=0; trim(txt); window_writeln(&win, txt);
            } else if (startswith(tag,"span")) {
                const char* sattr = 0; char* a = tag; while(*a){ if(startswith(a,"style=")){ sattr=a+6; break; } a++; }
                if(sattr && (*sattr=='"' || *sattr=='\'')) { char qch=*sattr++; char tmp[96]; int ti=0; while(*sattr && *sattr!=qch && ti<95) tmp[ti++]=*sattr++; tmp[ti]=0; style_t st = parse_style(tmp,(style_t){cur_fg,cur_bg}); cur_fg=st.fg; cur_bg=st.bg; }
                int p=j+1; while(buf[p] && !(buf[p]=='<' && (buf[p+1]=='/'||buf[p+1]==0))) p++; char txt[256]; int ti=0; for(int z=j+1; z<p && ti<255; ++z) txt[ti++]=buf[z]; txt[ti]=0; trim(txt);
                window_write(&win, txt);
            } else if (startswith(tag,"script")) {
                int p=j+1; while(buf[p] && !(buf[p]=='<' && startswith(&buf[p+1],"/script"))) p++;
                char js[256]; int ti=0; for(int z=j+1; z<p && ti<255; ++z) js[ti++]=buf[z]; js[ti]=0;
                if (js[0]){
                    const char* m=js;
                    const char* a1 = "alert('"; const char* c1 = "console.log('";
                    for(int x=0; m[x]; ++x){ int ok=1; for(int y=0; a1[y]; ++y){ if(!m[x+y]||m[x+y]!=a1[y]){ ok=0; break; } } if(ok){ int s=x+7; char t[128]; int ti2=0; while(m[s] && !(m[s]=='\''&&m[s+1]==')') && ti2<127) t[ti2++]=m[s++]; t[ti2]=0; window_writeln(&win, t); break; } }
                    for(int x=0; m[x]; ++x){ int ok=1; for(int y=0; c1[y]; ++y){ if(!m[x+y]||m[x+y]!=c1[y]){ ok=0; break; } } if(ok){ int s=x+13; char t[128]; int ti2=0; while(m[s] && !(m[s]=='\''&&m[s+1]==')') && ti2<127) t[ti2++]=m[s++]; t[ti2]=0; window_writeln(&win, t); break; } }
                }
            }
            i = j+1;
        } else {
            window_putc(&win, buf[i++]);
        }
    }

    window_putc(&win,'\n');
    return 0;
}
