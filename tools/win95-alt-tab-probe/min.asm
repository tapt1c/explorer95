.386p
_DATA segment dword public use32 'DATA'
public ProbeDDB
ProbeDDB label byte
dd 0
dw 030ah,0
db 0,1
dw 0
db 'EX95MIN0'
dd 80000000h,offset Control,0,0,0,0,0,0,0,0,0,80,0,0,0
assume cs:_DATA,ds:_DATA
Control proc near
xor eax,eax
clc
ret
Control endp
_DATA ends
end
