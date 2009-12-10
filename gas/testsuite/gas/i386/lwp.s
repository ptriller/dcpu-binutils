# Check 64bit LWP instructions

	.allow_index_reg
	.text
_start:

	llwpcb %ax
	llwpcb %cx
	llwpcb %dx
	llwpcb %bx
	llwpcb %sp
	llwpcb %bp
	llwpcb %si
	llwpcb %di
	llwpcb %eax
	llwpcb %ecx
	llwpcb %edx
	llwpcb %ebx
	llwpcb %esp
	llwpcb %ebp
	llwpcb %esi
	llwpcb %edi

	slwpcb %edi
	slwpcb %esi
	slwpcb %ebp
	slwpcb %esp
	slwpcb %ebx
	slwpcb %edx
	slwpcb %ecx
	slwpcb %eax
	slwpcb %di
	slwpcb %si
	slwpcb %bp
	slwpcb %sp
	slwpcb %bx
	slwpcb %dx
	slwpcb %cx
	slwpcb %ax

	lwpins $0x1234, %eax, %ax
	lwpins $0x1234, %ecx, %cx
	lwpins $0x1234, %edx, %dx
	lwpins $0x1234, %ebx, %bx
	lwpins $0x1234, %esp, %sp
	lwpins $0x1234, %ebp, %bp
	lwpins $0x1234, %esi, %si
	lwpins $0x1234, %edi, %di
	lwpins $0x12345678, %edi, %eax
	lwpins $0x12345678, %esi, %ecx
	lwpins $0x12345678, %ebp, %edx
	lwpins $0x12345678, %esp, %ebx
	lwpins $0x12345678, %ebx, %esp
	lwpins $0x12345678, %edx, %ebp
	lwpins $0x12345678, %ecx, %esi
	lwpins $0x12345678, %eax, %edi

	lwpval $0x1234, %eax, %ax
	lwpval $0x1234, %ecx, %cx
	lwpval $0x1234, %edx, %dx
	lwpval $0x1234, %ebx, %bx
	lwpval $0x1234, %esp, %sp
	lwpval $0x1234, %ebp, %bp
	lwpval $0x1234, %esi, %si
	lwpval $0x1234, %edi, %di
	lwpval $0x12345678, %edi, %eax
	lwpval $0x12345678, %esi, %ecx
	lwpval $0x12345678, %ebp, %edx
	lwpval $0x12345678, %esp, %ebx
	lwpval $0x12345678, %ebx, %esp
	lwpval $0x12345678, %edx, %ebp
	lwpval $0x12345678, %ecx, %esi
	lwpval $0x12345678, %eax, %edi

	lwpins $0x1234, (%eax), %ax
	lwpins $0x1234, (%ecx), %cx
	lwpins $0x1234, (%edx), %dx
	lwpins $0x1234, (%ebx), %bx
	lwpins $0x1234, (%esp), %sp
	lwpins $0x1234, (%ebp), %bp
	lwpins $0x1234, (%esi), %si
	lwpins $0x1234, (%edi), %di
	lwpins $0x12345678, (%edi), %eax
	lwpins $0x12345678, (%esi), %ecx
	lwpins $0x12345678, (%ebp), %edx
	lwpins $0x12345678, (%esp), %ebx
	lwpins $0x12345678, (%ebx), %esp
	lwpins $0x12345678, (%edx), %ebp
	lwpins $0x12345678, (%ecx), %esi
	lwpins $0x12345678, (%eax), %edi

	lwpval $0x1234, (%eax), %ax
	lwpval $0x1234, (%ecx), %cx
	lwpval $0x1234, (%edx), %dx
	lwpval $0x1234, (%ebx), %bx
	lwpval $0x1234, (%esp), %sp
	lwpval $0x1234, (%ebp), %bp
	lwpval $0x1234, (%esi), %si
	lwpval $0x1234, (%edi), %di
	lwpval $0x12345678, (%edi), %eax
	lwpval $0x12345678, (%esi), %ecx
	lwpval $0x12345678, (%ebp), %edx
	lwpval $0x12345678, (%esp), %ebx
	lwpval $0x12345678, (%ebx), %esp
	lwpval $0x12345678, (%edx), %ebp
	lwpval $0x12345678, (%ecx), %esi
	lwpval $0x12345678, (%eax), %edi

	lwpins $0x1234, 0xcafe(%eax), %ax
	lwpins $0x1234, 0xcafe(%ecx), %cx
	lwpins $0x1234, 0xcafe(%edx), %dx
	lwpins $0x1234, 0xcafe(%ebx), %bx
	lwpins $0x1234, 0xcafe(%esp), %sp
	lwpins $0x1234, 0xcafe(%ebp), %bp
	lwpins $0x1234, 0xcafe(%esi), %si
	lwpins $0x1234, 0xcafe(%edi), %di
	lwpins $0x12345678, 0xcafe(%edi), %eax
	lwpins $0x12345678, 0xcafe(%esi), %ecx
	lwpins $0x12345678, 0xcafe(%ebp), %edx
	lwpins $0x12345678, 0xcafe(%esp), %ebx
	lwpins $0x12345678, 0xcafe(%ebx), %esp
	lwpins $0x12345678, 0xcafe(%edx), %ebp
	lwpins $0x12345678, 0xcafe(%ecx), %esi
	lwpins $0x12345678, 0xcafe(%eax), %edi

	lwpval $0x1234, 0xcafe(%eax), %ax
	lwpval $0x1234, 0xcafe(%ecx), %cx
	lwpval $0x1234, 0xcafe(%edx), %dx
	lwpval $0x1234, 0xcafe(%ebx), %bx
	lwpval $0x1234, 0xcafe(%esp), %sp
	lwpval $0x1234, 0xcafe(%ebp), %bp
	lwpval $0x1234, 0xcafe(%esi), %si
	lwpval $0x1234, 0xcafe(%edi), %di
	lwpval $0x12345678, 0xcafe(%edi), %eax
	lwpval $0x12345678, 0xcafe(%esi), %ecx
	lwpval $0x12345678, 0xcafe(%ebp), %edx
	lwpval $0x12345678, 0xcafe(%esp), %ebx
	lwpval $0x12345678, 0xcafe(%ebx), %esp
	lwpval $0x12345678, 0xcafe(%edx), %ebp
	lwpval $0x12345678, 0xcafe(%ecx), %esi
	lwpval $0x12345678, 0xcafe(%eax), %edi
