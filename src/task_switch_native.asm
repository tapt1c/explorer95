; Dynamic Win95 VKD filter. Chained filter runs first. Interrupt masking
; serializes the tiny bounded core only; no USER calls, I/O, or client pointers.
.386p
extrn CoreControl_:near
extrn CoreScan_:near
_DATA segment dword public use32 'DATA'
db 16 dup(0)
public SwitchDDB
SwitchDDB label byte
dd 0
dw 030ah,0
db 1,0
dw 0
db 'EX95TS01'
dd 80000000h,offset Control,0,0,0,0,0,0,0,0,0,80,0,0,0
prior dd 0
systemVM dd 0
installed dd 0
_DATA ends
_TEXT segment dword public use32 'CODE'
db 16 dup(0)
assume cs:_TEXT,ds:_DATA
Control proc near
cmp eax,1bh
je Install
cmp eax,1ch
je Remove
cmp eax,23h
je Ioctl
clc
ret
Install:
pushad
int 20h
dd 10003h
mov systemVM,ebx
mov eax,0d0012h
mov esi,offset Keyboard
int 20h
dd 10090h
jc InstallFailed
mov prior,esi
mov installed,1
popad
clc
ret
InstallFailed:
popad
stc
ret
Remove:
; Unhook_Device_Service's carry is returned: failed unhook forbids unload.
cmp installed,0
je Removed
pushad
mov eax,0d0012h
mov esi,offset Keyboard
int 20h
dd 1011ch
popad
ret
Removed:
clc
ret
Ioctl:
pushfd
cli
pushad
int 20h
dd 1003fh
mov ecx,eax
mov eax,[esi+12]
mov edx,[esi+40]
mov ebx,[esi+44]
call CoreControl_
mov [esp+28],eax
popad
popfd
clc
ret
Control endp
Keyboard proc near
; Win95 service hook header also lets VMM unlink us from a hook chain.
jmp short KeyboardBody
jmp dword ptr prior
dd 0
KeyboardBody:
call dword ptr prior
jc OriginalResult
pushfd
cli
pushad
movzx ebp,cl
int 20h
dd 1003fh
mov edx,eax
int 20h
dd 10001h
cmp ebx,systemVM
mov ebx,0
jne ForeignVM
inc ebx
ForeignVM:
mov eax,ebp
call CoreScan_
test eax,eax
jnz Consume
popad
popfd
OriginalResult:
ret
Consume:
popad
popfd
stc
ret
Keyboard endp
_TEXT ends
end
