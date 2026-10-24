#define WIN32_LEAN_AND_MEAN
#include "taskbar_query.h"
typedef struct Calculation { const char *next; int error,depth; } Calculation;
static BOOL Space(char c) { return c==' ' || c=='\t' || c=='\r' || c=='\n'; }
static BOOL Prefix(const char *text,const char *prefix)
{
    while(*prefix) { char c=*text++; if(c>='A' && c<='Z') c+=(char)('a'-'A'); if(c!=*prefix++) return FALSE; }
    return TRUE;
}
static void Skip(Calculation *c) { while(Space(*c->next)) ++c->next; }
static double Expression(Calculation *c);
static double Bounded(Calculation *c,double value)
{
    if(value>1000000000000.0 || value< -1000000000000.0) c->error=QUERY_RANGE;
    return c->error ? 0 : value;
}
static double Factor(Calculation *c)
{
    double value=0,scale=1; BOOL negative=FALSE,digit=FALSE;
    Skip(c);
    if(++c->depth>16) { c->error=QUERY_INVALID; --c->depth; return 0; }
    while(*c->next=='+' || *c->next=='-') { if(*c->next++=='-') negative=!negative; Skip(c); }
    if(*c->next=='(') {
        ++c->next; value=Expression(c); Skip(c);
        if(*c->next==')') ++c->next; else c->error=QUERY_INVALID;
    } else {
        while(*c->next>='0' && *c->next<='9') { digit=TRUE; value=Bounded(c,value*10+(*c->next++-'0')); }
        if(*c->next=='.' || *c->next==',') {
            ++c->next;
            while(*c->next>='0' && *c->next<='9') { digit=TRUE; scale*=0.1; value+=(*c->next++-'0')*scale; }
        }
        if(!digit) c->error=QUERY_INVALID;
    }
    --c->depth; return Bounded(c,negative ? -value : value);
}
static double Product(Calculation *c)
{
    double value=Factor(c); Skip(c);
    while(!c->error && (*c->next=='*' || *c->next=='/')) {
        char operation=*c->next++; double right=Factor(c);
        if(c->error) break;
        if(operation=='/' && right==0) { c->error=QUERY_ZERO; break; }
        if(operation=='/' && (right<0 ? -right : right)<1 &&
           (value<0 ? -value : value)>1000000000000.0*(right<0 ? -right : right)) { c->error=QUERY_RANGE; break; }
        value=Bounded(c,operation=='*' ? value*right : value/right); Skip(c);
    }
    return value;
}
static double Expression(Calculation *c)
{
    double value=Product(c); Skip(c);
    while(!c->error && (*c->next=='+' || *c->next=='-')) {
        char operation=*c->next++; double right=Product(c);
        value=Bounded(c,operation=='+' ? value+right : value-right); Skip(c);
    }
    return value;
}
static void Format(double value,char *text)
{
    double power=1000000000000.0; char *p=text,*decimal; int i,digit;
    if(value<0) { *p++='-'; value=-value; }
    /* Eight decimal places, rounded without printf/floor/64-bit CRT helpers. */
    value+=0.000000005;
    while(power>1 && value<power) power/=10;
    do { digit=(int)(value/power); if(digit>9) digit=9; *p++=(char)('0'+digit); value-=digit*power; power/=10; } while(power>=1);
    decimal=p; *p++='.';
    for(i=0;i<8;++i) { value*=10; digit=(int)value; if(digit>9) digit=9; *p++=(char)('0'+digit); value-=digit; }
    while(p>decimal+1 && p[-1]=='0') --p;
    if(p==decimal+1) p=decimal;
    *p=0; if(!lstrcmpA(text,"-0")) lstrcpyA(text,"0");
}
int TaskbarQueryEvaluate(const char *text,char *result)
{
    const char *p; BOOL digit=FALSE; Calculation c; double value;
    result[0]=0;
    if(lstrlenA(text)>512) return QUERY_COMMAND;
    for(p=text;*p;++p) {
        if(*p>='0' && *p<='9') digit=TRUE;
        else if(!Space(*p) && *p!='.' && *p!=',' && *p!='+' && *p!='-' && *p!='*' && *p!='/' && *p!='(' && *p!=')') return QUERY_COMMAND;
    }
    if(!digit) return QUERY_COMMAND;
    c.next=text; c.error=c.depth=0; value=Expression(&c); Skip(&c);
    if(c.error) return c.error;
    if(*c.next) return QUERY_INVALID;
    Format(value,result); return QUERY_VALUE;
}
BOOL TaskbarQuerySplit(const char *text,char *file,int fileSize,char *arguments,int argumentSize)
{
    char expanded[1024]; const char *begin,*end,*rest; DWORD length; int size;
    file[0]=arguments[0]=0;
    length=ExpandEnvironmentStringsA(text,expanded,sizeof(expanded));
    if(!length || length>sizeof(expanded)) return FALSE;
    begin=expanded; while(Space(*begin)) ++begin;
    size=lstrlenA(begin); while(size && Space(begin[size-1])) --size;
    ((char *)begin)[size]=0; if(!size) return FALSE;
    /* Recognise protocols before splitting: a URL may contain spaces which the
     * installed handler is responsible for quoting/escaping. */
    if(Prefix(begin,"http://") || Prefix(begin,"https://") || Prefix(begin,"ftp://") || GetFileAttributesA(begin)!=INVALID_FILE_ATTRIBUTES) {
        if(size>=fileSize) return FALSE;
        lstrcpyA(file,begin); return TRUE;
    }
    if(*begin=='"') { ++begin; end=begin; while(*end && *end!='"') ++end; if(!*end) return FALSE; rest=end+1; if(*rest && !Space(*rest)) return FALSE; }
    else { end=begin; while(*end && !Space(*end)) ++end; rest=end; }
    while(Space(*rest)) ++rest;
    size=(int)(end-begin); if(!size || size>=fileSize || lstrlenA(rest)>=argumentSize) return FALSE;
    lstrcpynA(file,begin,size+1); lstrcpyA(arguments,rest); return TRUE;
}
BOOL TaskbarQueryMatch(const char *label,const char *query)
{
    int size=lstrlenA(query),remaining=lstrlenA(label);
    if(!size) return FALSE;
    while(remaining>=size) {
        if(CompareStringA(LOCALE_SYSTEM_DEFAULT,NORM_IGNORECASE,label,size,query,size)==CSTR_EQUAL) return TRUE;
        ++label; --remaining;
    }
    return FALSE;
}
void TaskbarQueryResolve(char *file,int capacity)
{
    static const char *extensions[]={NULL,".exe",".com",".bat",".pif"};
    const char *p; char resolved[MAX_PATH]; DWORD length=0; unsigned i;
    for(p=file;*p;++p) if(*p=='/' || *p=='\\' || *p==':') return;
    for(i=0;i<sizeof(extensions)/sizeof(extensions[0]) && !length;++i) length=SearchPathA(NULL,file,extensions[i],MAX_PATH,resolved,NULL);
    if(length && length<MAX_PATH && length<(DWORD)capacity) lstrcpyA(file,resolved);
}
