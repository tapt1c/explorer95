/* Filename/metadata predicates, bounded location lists and saved query data.
 * No file contents are read, no query text is interpreted as a command. */
#define WIN32_LEAN_AND_MEAN
#include "search_query.h"
static BOOL Next(const char **list,char *text,DWORD capacity)
{
    DWORD n=0; BOOL quote=FALSE; const char *p=*list;
    while(*p==';' || *p=='\r' || *p=='\n' || *p==' ' || *p=='\t') ++p;
    if(!*p) { *list=p; text[0]=0; return FALSE; }
    while(*p && (quote || (*p!=';' && *p!='\r' && *p!='\n'))) {
        const char *next=CharNextA(p); DWORD width=(DWORD)(next-p);
        if(*p=='"') quote=!quote;
        else {
            if(n+width>=capacity) { text[0]=0; *list=p; return FALSE; }
            while(p<next) text[n++]=*p++;
            continue;
        }
        p=next;
    }
    while(n && (text[n-1]==' ' || text[n-1]=='\t')) --n;
    text[n]=0; *list=p;
    if(quote) text[0]=0;
    return TRUE;
}
BOOL SearchAbsolutePath(const char *path)
{
    return path && ((path[0] && path[1]==':' && path[2]=='\\' &&
        ((path[0]>='A' && path[0]<='Z') || (path[0]>='a' && path[0]<='z'))) ||
        (path[0]=='\\' && path[1]=='\\' && path[2] && path[2]!='.' && path[2]!='?'));
}
BOOL SearchPathWithin(const char *path,const char *folder)
{
    DWORD n=lstrlenA(folder); char prefix[MAX_PATH];
    if(!n || n>=MAX_PATH || (DWORD)lstrlenA(path)<n) return FALSE;
    lstrcpynA(prefix,path,n+1);
    return !lstrcmpiA(prefix,folder) && (!path[n] || folder[n-1]=='\\' || path[n]=='\\');
}
static BOOL Number(const char *text,DWORD *value)
{
    DWORD n=0; const char *p=text;
    while(*p) { if(*p<'0' || *p>'9' || n>(0xffffffffUL-(DWORD)(*p-'0'))/10) return FALSE; n=n*10+*p++-'0'; }
    *value=n; return TRUE;
}
static BOOL Date(const char *text,DWORD *value)
{
    int i,year,month,day; static const int days[]={31,28,31,30,31,30,31,31,30,31,30,31};
    *value=0; if(!text[0]) return TRUE;
    if(lstrlenA(text)!=10 || text[4]!='-' || text[7]!='-') return FALSE;
    for(i=0;i<10;++i) if(i!=4 && i!=7 && (text[i]<'0' || text[i]>'9')) return FALSE;
    year=(text[0]-'0')*1000+(text[1]-'0')*100+(text[2]-'0')*10+text[3]-'0';
    month=(text[5]-'0')*10+text[6]-'0'; day=(text[8]-'0')*10+text[9]-'0';
    if(year<1601 || month<1 || month>12 || day<1 || day>days[month-1]+(month==2 && !(year%4) && (year%100 || !(year%400)))) return FALSE;
    *value=year*10000+month*100+day; return TRUE;
}
static BOOL ValidMasks(const char *list)
{
    const char *p=list; char part[512];
    while(*p) {
        if(!Next(&p,part,sizeof(part))) return !*p;
        if(!part[0]) return FALSE;
    }
    return TRUE;
}
void SearchOptionsDefault(SearchOptions *options,const char *directory,BOOL hidden)
{
    ZeroMemory(options,sizeof(*options)); options->recursive=TRUE; options->hidden=options->system=!!hidden;
    lstrcpynA(options->locations,directory,sizeof(options->locations));
}
DWORD SearchQueryPrepare(SearchQuery *query,const SearchOptions *options)
{
    const char *p=options->locations; char part[MAX_PATH],full[MAX_PATH]; DWORD n,i,j; BOOL quoted=FALSE;
    ZeroMemory(query,sizeof(*query)); query->options=*options;
    if(options->mode>2 || options->exact>1 || options->recursive>1 || options->hidden>1 || options->system>1 || options->useIndex>1) return ERROR_INVALID_DATA;
    if(!ValidMasks(options->name) || !ValidMasks(options->types) || !ValidMasks(options->excludeNames)) return ERROR_INVALID_DATA;
    if(!Number(options->minimum,&n) || n>4194303) return ERROR_INVALID_DATA;
    query->hasMinimum=!!options->minimum[0]; query->minimum=n*1024;
    if(!Number(options->maximum,&n) || n>4194303) return ERROR_INVALID_DATA;
    query->hasMaximum=!!options->maximum[0]; query->maximum=n*1024;
    if(query->hasMinimum && query->hasMaximum && query->minimum>query->maximum) return ERROR_INVALID_DATA;
    if(!Date(options->after,&query->after) || !Date(options->before,&query->before) ||
       (query->after && query->before && query->after>query->before)) return ERROR_INVALID_DATA;
    while(*p) {
        if(!Next(&p,part,sizeof(part))) { if(*p) return ERROR_BAD_PATHNAME; break; }
        if(!part[0] || query->rootCount==SEARCH_ROOTS) return ERROR_BAD_PATHNAME;
        n=GetFullPathNameA(part,MAX_PATH,full,NULL);
        if(!n || n>=MAX_PATH || !SearchAbsolutePath(full)) return ERROR_BAD_PATHNAME;
        for(i=0;i<n;++i) if(full[i]=='/') full[i]='\\';
        while(n>3 && full[n-1]=='\\') full[--n]=0;
        for(i=0;i<query->rootCount;++i)
            if(!lstrcmpiA(full,query->roots[i]) || (options->recursive && SearchPathWithin(full,query->roots[i]))) break;
        if(i<query->rootCount) continue;
        if(options->recursive) for(i=0;i<query->rootCount;) {
            if(SearchPathWithin(query->roots[i],full)) {
                for(j=i+1;j<query->rootCount;++j) lstrcpyA(query->roots[j-1],query->roots[j]);
                --query->rootCount;
            } else ++i;
        }
        lstrcpyA(query->roots[query->rootCount++],full);
    }
    /* Detect a malformed quoted exclusion before any directory enumeration. */
    for(p=options->excludeFolders;*p;++p) if(*p=='"') quoted=!quoted;
    if(quoted || !query->rootCount) return ERROR_BAD_PATHNAME;
    p=options->excludeFolders; query->options.excludeFolders[0]=0;
    while(*p) {
        if(!Next(&p,part,sizeof(part))) { if(*p) return ERROR_BAD_PATHNAME; break; }
        if(!part[0]) return ERROR_BAD_PATHNAME;
        if(SearchAbsolutePath(part)) {
            n=GetFullPathNameA(part,MAX_PATH,full,NULL);
            if(!n || n>=MAX_PATH) return ERROR_BAD_PATHNAME;
            while(n>3 && full[n-1]=='\\') full[--n]=0;
            lstrcpyA(part,full);
        }
        n=lstrlenA(query->options.excludeFolders);
        if(n+lstrlenA(part)+4>=SEARCH_EXCLUSIONS_SIZE) return ERROR_INSUFFICIENT_BUFFER;
        wsprintfA(query->options.excludeFolders+n,"%s\"%s\"",n ? ";" : "",part);
    }
    /* Canonical root order makes equivalent location lists share one index. */
    for(i=1;i<query->rootCount;++i) for(j=i;j && lstrcmpiA(query->roots[j-1],query->roots[j])>0;--j) {
        lstrcpyA(full,query->roots[j]); lstrcpyA(query->roots[j],query->roots[j-1]); lstrcpyA(query->roots[j-1],full);
    }
    return ERROR_SUCCESS;
}
static BOOL Wildcard(const char *pattern,const char *name)
{
    char a[514],b[MAX_PATH]; const char *p=a,*n=b,*star=NULL,*retry=NULL;
    lstrcpynA(a,pattern,sizeof(a)); lstrcpynA(b,name,sizeof(b));
    CharUpperBuffA(a,lstrlenA(a)); CharUpperBuffA(b,lstrlenA(b));
    if(!lstrcmpA(a,"*.*")) return TRUE;
    while(*n) {
        int width=(int)(CharNextA(n)-n),pw=(int)(CharNextA(p)-p);
        if(*p=='?' || (pw==width && p[0]==n[0] && (width==1 || p[1]==n[1]))) { p=*p=='?' ? p+1 : p+pw; n+=width; }
        else if(*p=='*') { star=++p; retry=n; }
        else if(star) { p=star; n=retry=CharNextA(retry); }
        else return FALSE;
    }
    while(*p=='*') ++p;
    return !*p;
}
static BOOL Masks(const char *list,const char *name,BOOL contains,BOOL emptyMatches)
{
    const char *p=list; char part[512],mask[514]; BOOL any=FALSE;
    if(!*list) return emptyMatches;
    while(*p && Next(&p,part,sizeof(part))) {
        const char *c; BOOL wildcard=FALSE;
        if(!part[0]) continue;
        any=TRUE;
        for(c=part;*c;c=CharNextA(c)) if(*c=='*' || *c=='?') wildcard=TRUE;
        if(contains && !wildcard) wsprintfA(mask,"*%s*",part); else lstrcpyA(mask,part);
        if(Wildcard(mask,name)) return TRUE;
    }
    return !any && emptyMatches;
}
BOOL SearchQueryExcluded(const SearchQuery *query,const char *path)
{
    const char *p=query->options.excludeFolders; char part[MAX_PATH];
    while(*p && Next(&p,part,sizeof(part))) {
        if(SearchAbsolutePath(part)) { if(SearchPathWithin(path,part)) return TRUE; }
        else {
            const char *s=path,*start; char component[MAX_PATH];
            while(*s) {
                DWORD n; start=s; while(*s && *s!='\\') s=CharNextA(s); n=(DWORD)(s-start);
                lstrcpynA(component,start,n+1);
                if(part[0] && Wildcard(part,component)) return TRUE;
                if(*s) ++s;
            }
        }
    }
    return FALSE;
}
BOOL SearchQueryMatch(const SearchQuery *query,const SearchStoreRecord *record)
{
    const SearchOptions *o=&query->options; const char *name=SearchRecordName(record);
    if((o->mode==1 && record->folder) || (o->mode==2 && !record->folder) ||
       (!o->hidden && (record->attributes&FILE_ATTRIBUTE_HIDDEN)) || (!o->system && (record->attributes&FILE_ATTRIBUTE_SYSTEM))) return FALSE;
    if(!Masks(o->name,name,!o->exact,TRUE) || Masks(o->excludeNames,name,FALSE,FALSE) ||
       (!record->folder && !Masks(o->types,name,FALSE,TRUE))) return FALSE;
    if(query->hasMinimum || query->hasMaximum) {
        if(record->folder || (query->hasMinimum && !record->sizeHigh && record->sizeLow<query->minimum) ||
           (query->hasMaximum && (record->sizeHigh || record->sizeLow>query->maximum))) return FALSE;
    }
    if(query->after || query->before) {
        FILETIME local; SYSTEMTIME time; DWORD date;
        if(!FileTimeToLocalFileTime(&record->modified,&local) || !FileTimeToSystemTime(&local,&time)) return FALSE;
        date=time.wYear*10000+time.wMonth*100+time.wDay;
        if((query->after && date<query->after) || (query->before && date>query->before)) return FALSE;
    }
    return TRUE;
}
static void Section(int slot,char *section) { wsprintfA(section,"FileSearch%d",slot); }
BOOL SearchQuerySlotUsed(const char *ini,int slot)
{
    char section[32],data[4];
    if(slot<0 || slot>=SEARCH_SAVED) return TRUE;
    Section(slot,section); return GetPrivateProfileSectionA(section,data,sizeof(data),ini)>0;
}
BOOL SearchQueryWrite(const char *ini,int slot,const char *label,const SearchOptions *o)
{
    char section[32],text[SEARCH_LOCATIONS_SIZE+2]; BOOL okay=TRUE;
    if(slot<0 || slot>=SEARCH_SAVED) return FALSE;
    Section(slot,section);
    if(!o) return WritePrivateProfileStringA(section,NULL,NULL,ini);
#define QUERY_TEXT(key,value) wsprintfA(text,"!%s",value); okay &= WritePrivateProfileStringA(section,key,text,ini)
#define NUMBER(key,value) wsprintfA(text,"%lu",(DWORD)(value)); okay &= WritePrivateProfileStringA(section,key,text,ini)
    QUERY_TEXT("Label",label); QUERY_TEXT("Name",o->name); QUERY_TEXT("Locations",o->locations); QUERY_TEXT("ExcludeNames",o->excludeNames);
    QUERY_TEXT("ExcludeFolders",o->excludeFolders); QUERY_TEXT("Types",o->types); QUERY_TEXT("Minimum",o->minimum); QUERY_TEXT("Maximum",o->maximum);
    QUERY_TEXT("After",o->after); QUERY_TEXT("Before",o->before);
    NUMBER("Mode",o->mode); NUMBER("Exact",o->exact); NUMBER("Recursive",o->recursive);
    NUMBER("Hidden",o->hidden); NUMBER("System",o->system); NUMBER("UseIndex",o->useIndex);
#undef QUERY_TEXT
#undef NUMBER
    return okay;
}
static BOOL ReadText(const char *ini,const char *section,const char *key,char *out,DWORD capacity)
{
    char buffer[SEARCH_LOCATIONS_SIZE+2]; DWORD n=GetPrivateProfileStringA(section,key,"",buffer,sizeof(buffer),ini);
    if(!n) { out[0]=0; return TRUE; }
    if(buffer[0]!='!' || n>=capacity+1 || n>=sizeof(buffer)-1) return FALSE;
    lstrcpyA(out,buffer+1); return TRUE;
}
BOOL SearchQueryRead(const char *ini,int slot,char *label,SearchOptions *o)
{
    char section[32]; BOOL okay=TRUE;
    if(slot<0 || slot>=SEARCH_SAVED) return FALSE;
    Section(slot,section); ZeroMemory(o,sizeof(*o));
#define QUERY_TEXT(key,field) okay &= ReadText(ini,section,key,o->field,sizeof(o->field))
#define NUMBER(key,field) o->field=GetPrivateProfileIntA(section,key,0,ini)
    okay &= ReadText(ini,section,"Label",label,64);
    QUERY_TEXT("Name",name); QUERY_TEXT("Locations",locations); QUERY_TEXT("ExcludeNames",excludeNames); QUERY_TEXT("ExcludeFolders",excludeFolders);
    QUERY_TEXT("Types",types); QUERY_TEXT("Minimum",minimum); QUERY_TEXT("Maximum",maximum); QUERY_TEXT("After",after); QUERY_TEXT("Before",before);
    NUMBER("Mode",mode); NUMBER("Exact",exact); NUMBER("Recursive",recursive);
    NUMBER("Hidden",hidden); NUMBER("System",system); NUMBER("UseIndex",useIndex);
#undef QUERY_TEXT
#undef NUMBER
    return okay && label[0] && o->locations[0];
}
