; Disposable Win95 VM experiment. No installation or persistent configuration.
; Only Alt+Tab and chooser cancellation are consumed during a 200 ms UI lease.
.386p
_DATA segment dword public use32 'DATA'
db 16 dup(0)
public ProbeDDB
ProbeDDB label byte
dd 0
dw 030ah,0
db 0,1
dw 0
db 'EX95KEY0'
dd 80000000h,offset Control,0,0,0,0,0,0,0,0,0,80,0,0,0
prior dd 0
initState dd 0
systemVM dd 0
owner dd 0
lease dd 0
enabled dd 0
choosing dd 0
cancelled dd 0
altKeys dd 0
ctrlKeys dd 0
shiftKeys dd 0
extended dd 0
counts dd 0,0,0,0,0
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
mov initState,1
popad
clc
ret
InstallFailed:
mov initState,2
popad
clc
ret
Remove:
mov enabled,0
mov choosing,0
cmp prior,0
je Removed
mov eax,0d0012h
mov esi,offset Keyboard
int 20h
dd 1011ch
ret
Removed:
clc
ret
Ioctl:
; VWIN32 supplies ESI. Experiment never dereferences application pointers.
mov eax,[esi+12]
test eax,eax
jz Open
cmp eax,-1
je Close
cmp eax,7
je InitQuery
cmp eax,1
je Renew
mov edx,[esi+40]
cmp edx,owner
jne Deny
cmp eax,2
jb Deny
cmp eax,6
ja Deny
sub eax,2
mov eax,counts[eax*4]
add eax,1000h
clc
ret
InitQuery:
mov eax,initState
add eax,1000h
clc
ret
Open:
cmp owner,0
jne Deny
xor eax,eax
clc
ret
Close:
mov eax,[esi+40]
cmp eax,owner
jne Closed
mov enabled,0
mov choosing,0
mov cancelled,0
mov owner,0
Closed:
xor eax,eax
clc
ret
Renew:
cmp initState,1
jne Deny
mov eax,[esi+40]
test eax,eax
jz Deny
cmp owner,0
jne HaveOwner
mov owner,eax
HaveOwner:
cmp eax,owner
jne Deny
int 20h
dd 1003fh
mov lease,eax
mov enabled,1
xor eax,eax
clc
ret
Deny:
mov eax,5
clc
ret
Control endp
Keyboard proc near
jmp short KeyboardBody
jmp dword ptr prior
dd 0
KeyboardBody:
call dword ptr prior
jc OriginalResult
pushfd
pushad
movzx eax,cl
cmp al,0e0h
je Prefix
mov edx,extended
mov extended,0
mov ebp,eax
and eax,7fh
mov ebx,1
test edx,edx
jz Modifiers
mov ebx,2
Modifiers:
cmp al,38h
je AltChange
cmp al,1dh
je CtrlChange
cmp al,2ah
je LeftShift
cmp al,36h
je RightShift
cmp al,0fh
jne LeaseCheck
inc counts[16]
LeaseCheck:
cmp enabled,0
je Pass
int 20h
dd 1003fh
sub eax,lease
cmp eax,200
ja Expired
int 20h
dd 10001h
cmp ebx,systemVM
jne Pass
mov eax,ebp
and eax,7fh
cmp eax,0fh
je TabKey
cmp choosing,0
je Pass
cmp eax,1
jne Pass
test ebp,80h
jnz Consume
cmp cancelled,0
jne Consume
inc counts[12]
mov cancelled,1
jmp Consume
TabKey:
test ebp,80h
jnz TabUp
cmp altKeys,0
je Pass
cmp ctrlKeys,0
jne Pass
cmp cancelled,0
jne Consume
mov choosing,1
cmp shiftKeys,0
jne Back
inc counts[0]
jmp Consume
Back:
inc counts[4]
jmp Consume
TabUp:
cmp choosing,0
jne Consume
jmp Pass
Expired:
mov choosing,0
mov cancelled,0
jmp Pass
Prefix:
mov extended,1
jmp Pass
AltChange:
test ebp,80h
jnz AltUp
or altKeys,ebx
jmp Pass
AltUp:
not ebx
and altKeys,ebx
cmp altKeys,0
jne Pass
cmp choosing,0
je Pass
mov choosing,0
cmp cancelled,0
jne ResetCancel
inc counts[8]
ResetCancel:
mov cancelled,0
; Modifier release must reach Windows, preventing stuck Alt state.
jmp Pass
CtrlChange:
test ebp,80h
jnz CtrlUp
or ctrlKeys,ebx
jmp Pass
CtrlUp:
not ebx
and ctrlKeys,ebx
jmp Pass
LeftShift:
mov ebx,1
jmp ShiftChange
RightShift:
mov ebx,2
ShiftChange:
test ebp,80h
jnz ShiftUp
or shiftKeys,ebx
jmp Pass
ShiftUp:
not ebx
and shiftKeys,ebx
Pass:
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
