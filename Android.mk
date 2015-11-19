LOCAL_PATH := $(call my-dir)
CLANG_ROOT_PATH := $(LOCAL_PATH)

FORCE_BUILD_LLVM_DISABLE_NDEBUG ?= false
# Legality check: FORCE_BUILD_LLVM_DISABLE_NDEBUG should consist of one word -- either "true" or "false".
ifneq "$(words $(FORCE_BUILD_LLVM_DISABLE_NDEBUG))$(words $(filter-out true false,$(FORCE_BUILD_LLVM_DISABLE_NDEBUG)))" "10"
  $(error FORCE_BUILD_LLVM_DISABLE_NDEBUG may only be true, false, or unset)
endif

FORCE_BUILD_LLVM_DEBUG ?= false
# Legality check: FORCE_BUILD_LLVM_DEBUG should consist of one word -- either "true" or "false".
ifneq "$(words $(FORCE_BUILD_LLVM_DEBUG))$(words $(filter-out true false,$(FORCE_BUILD_LLVM_DEBUG)))" "10"
  $(error FORCE_BUILD_LLVM_DEBUG may only be true, false, or unset)
endif

.PHONY: clang-toolchain
clang-toolchain: \
    $(TARGET_OUT_HEADERS)/clang/arm_neon.h \
    clang \
    FileCheck \
    llvm-as \
    llvm-dis \
    llvm-link \
    LLVMgold \
    libprofile_rt \

# We only build the 32-bit versions of these libraries when we're building a
# 32-bit target. We also build these for Linux, but we don't build them for
# Darwin. As long as we add them to the 32-bit target builds, they will get
# built for Linux too.
ifdef TARGET_2ND_ARCH
clang-toolchain: \
    libprofile_rt_32 \
    libasan_32 \
    libasan_cxx_32 \
    libubsan_standalone_32 \
    libubsan_standalone_cxx_32 \

endif

ifneq ($(HOST_OS),darwin)
clang-toolchain: \
    host_cross_clang \
    libtsan \
    libtsan_cxx \

endif

ifeq ($(TARGET_ARCH),arm)
clang-toolchain: \
    $(ADDRESS_SANITIZER_RUNTIME_LIBRARY) \
    libasan \
    libasan_cxx \
    libubsan_standalone \
    libubsan_standalone_cxx \

endif

include $(CLEAR_VARS)

subdirs := $(addprefix $(LOCAL_PATH)/,$(addsuffix /Android.mk, \
  lib/Analysis \
  lib/AST \
  lib/ASTMatchers \
  lib/ARCMigrate \
  lib/Basic \
  lib/CodeGen \
  lib/Driver \
  lib/Edit \
  lib/Format \
  lib/Frontend \
  lib/Frontend/Rewrite \
  lib/FrontendTool \
  lib/Headers \
  lib/Index \
  lib/Lex \
  lib/Parse \
  lib/Rewrite \
  lib/Sema \
  lib/Serialization \
  lib/StaticAnalyzer/Checkers \
  lib/StaticAnalyzer/Core \
  lib/StaticAnalyzer/Frontend \
  lib/Tooling \
  tools/driver \
  tools/libclang \
  utils/TableGen \
  ))

include $(LOCAL_PATH)/clang.mk
include $(LOCAL_PATH)/shared_clang.mk

include $(subdirs)
