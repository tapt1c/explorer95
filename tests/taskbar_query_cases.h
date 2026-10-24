#include "../src/taskbar_query.h"
static void __attribute__((noinline)) TaskbarQueryTests(const char *directory,const char *module)
{
    static const struct { const char *input,*expected; int status; } cases[]={
        {"6/2","3",QUERY_VALUE},{"2+3*4","14",QUERY_VALUE},{"(2+3)*4","20",QUERY_VALUE},
        {"-2 * -(3+1)","8",QUERY_VALUE},{"1.5+2,25","3.75",QUERY_VALUE},
        {"1/3","0.33333333",QUERY_VALUE},{"0.1+0.2","0.3",QUERY_VALUE},{"0/6","0",QUERY_VALUE},
        {"6/0","",QUERY_ZERO},{"1/(2-2)","",QUERY_ZERO},{"0/0","",QUERY_ZERO},
        {"6/","",QUERY_INVALID},{"(1+2","",QUERY_INVALID},{"1..2","",QUERY_INVALID},
        {"1 2","",QUERY_INVALID},{"999999999999*9999","",QUERY_RANGE},
        {"mspaint","",QUERY_COMMAND},{"http://example.invalid/6/2","",QUERY_COMMAND},
        {"C:\\file1.txt","",QUERY_COMMAND},{"paint.exe /x","",QUERY_COMMAND}
    };
    char answer[64],file[1024],arguments[1024]; static char path[4096],fixture[MAX_PATH]; unsigned i; BOOL hadPath;
    for(i=0;i<sizeof(cases)/sizeof(cases[0]);++i) {
        int status=TaskbarQueryEvaluate(cases[i].input,answer);
        if(status!=cases[i].status || lstrcmpA(answer,cases[i].expected)) { char note[200]; wsprintfA(note,"Query: input=%s status=%d result=%s\r\n",cases[i].input,status,answer); Log(note); }
        Check(status==cases[i].status && !lstrcmpA(answer,cases[i].expected),"bounded calculator handles precedence, fractions, invalid input and division by zero without execution");
    }
    Check(TaskbarQuerySplit("mspaint /x \"a file.bmp\"",file,sizeof(file),arguments,sizeof(arguments)) && !lstrcmpA(file,"mspaint") && !lstrcmpA(arguments,"/x \"a file.bmp\""),"command splitting preserves executable and quoted arguments without a command interpreter");
    Check(TaskbarQuerySplit("\"C:\\Program Files\\Own app.exe\" \"first arg\"",file,sizeof(file),arguments,sizeof(arguments)) && !lstrcmpA(file,"C:\\Program Files\\Own app.exe") && !lstrcmpA(arguments,"\"first arg\""),"quoted application paths preserve spaces and separate arguments");
    Check(!TaskbarQuerySplit("\"unterminated path",file,sizeof(file),arguments,sizeof(arguments)),"an unmatched command quote cannot execute a truncated path");
    Check(TaskbarQuerySplit(" HTTPS://example.invalid/a b ",file,sizeof(file),arguments,sizeof(arguments)) && !lstrcmpA(file,"HTTPS://example.invalid/a b") && !arguments[0],"the complete URL is passed to the installed handler, including its protocol");
    Check(TaskbarQueryMatch("Microsoft Paint","PAINT") && !TaskbarQueryMatch("Paint","calculator"),"Start menu search matches native labels without case sensitivity");
    hadPath=GetEnvironmentVariableA("PATH",path,sizeof(path))!=0;
    wsprintfA(fixture,"%s\\ex95-query-path.exe",directory);
    Check(CopyFileA(module,fixture,TRUE) && SetEnvironmentVariableA("PATH",directory),"prepare only our own PATH command fixture and process environment");
    lstrcpyA(file,"ex95-query-path"); TaskbarQueryResolve(file,sizeof(file));
    Check(TaskbarSameApplication(file,fixture),"a bare command finds its executable in PATH without requiring the extension");
    Check(SetEnvironmentVariableA("PATH",hadPath ? path : NULL) && DeleteFileA(fixture),"restore the process PATH and remove only our own command fixture");
}
