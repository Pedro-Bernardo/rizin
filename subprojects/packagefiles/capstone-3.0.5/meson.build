project('capstone', 'c', version: '3.0.5', meson_version: '>=0.55.0')

cs_files = [
  'arch/AArch64/AArch64BaseInfo.c',
  'arch/AArch64/AArch64Disassembler.c',
  'arch/AArch64/AArch64InstPrinter.c',
  'arch/AArch64/AArch64Mapping.c',
  'arch/AArch64/AArch64Module.c',
  'arch/ARM/ARMDisassembler.c',
  'arch/ARM/ARMInstPrinter.c',
  'arch/ARM/ARMMapping.c',
  'arch/ARM/ARMModule.c',
  'arch/Mips/MipsDisassembler.c',
  'arch/Mips/MipsInstPrinter.c',
  'arch/Mips/MipsMapping.c',
  'arch/Mips/MipsModule.c',
  'arch/PowerPC/PPCDisassembler.c',
  'arch/PowerPC/PPCInstPrinter.c',
  'arch/PowerPC/PPCMapping.c',
  'arch/PowerPC/PPCModule.c',
  'arch/Sparc/SparcDisassembler.c',
  'arch/Sparc/SparcInstPrinter.c',
  'arch/Sparc/SparcMapping.c',
  'arch/Sparc/SparcModule.c',
  'arch/SystemZ/SystemZDisassembler.c',
  'arch/SystemZ/SystemZInstPrinter.c',
  'arch/SystemZ/SystemZMapping.c',
  'arch/SystemZ/SystemZMCTargetDesc.c',
  'arch/SystemZ/SystemZModule.c',
  'arch/X86/X86ATTInstPrinter.c',
  'arch/X86/X86Disassembler.c',
  'arch/X86/X86DisassemblerDecoder.c',
  'arch/X86/X86IntelInstPrinter.c',
  'arch/X86/X86Mapping.c',
  'arch/X86/X86Module.c',
  'arch/XCore/XCoreDisassembler.c',
  'arch/XCore/XCoreInstPrinter.c',
  'arch/XCore/XCoreMapping.c',
  'arch/XCore/XCoreModule.c',
  'cs.c',
  'MCInst.c',
  'MCInstrDesc.c',
  'MCRegisterInfo.c',
  'SStream.c',
  'utils.c',
]

capstone_includes = [include_directories('include')]

libcapstone_c_args = [
  '-DCAPSTONE_X86_ATT_DISABLE_NO',
  '-DCAPSTONE_X86_REDUCE_NO',
  '-DCAPSTONE_USE_SYS_DYN_MEM',
  '-DCAPSTONE_DIET_NO',
  '-DCAPSTONE_HAS_ARM',
  '-DCAPSTONE_HAS_ARM64',
  '-DCAPSTONE_HAS_M68K',
  '-DCAPSTONE_HAS_M680X',
  '-DCAPSTONE_HAS_MIPS',
  '-DCAPSTONE_HAS_POWERPC',
  '-DCAPSTONE_HAS_SPARC',
  '-DCAPSTONE_HAS_SYSZ',
  '-DCAPSTONE_HAS_X86',
  '-DCAPSTONE_HAS_XCORE',
  '-DCAPSTONE_HAS_TMS320C64X',
]
warn_trunc_flag = '-Wno-error=stringop-truncation'
if meson.get_compiler('c').has_argument(warn_trunc_flag)
  libcapstone_c_args += warn_trunc_flag
endif

libcapstone = library('capstone', cs_files,
  c_args: libcapstone_c_args,
  include_directories: capstone_includes,
  implicit_include_directories: false
)

capstone_dep = declare_dependency(
  link_with: libcapstone,
  include_directories: capstone_includes
)
