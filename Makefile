# コンパイラ（この Makefile は GCC 9 以上または Clang 14 以上にしか対応していません）
# ※ Clang を使用する場合は、scan-build も使用可能にする必要があります
CC					= gcc

# アーカイバ（この Makefile は GNU ar でしか動作確認されていません）
AR					= ar

# CC が GCC であった場合
ifneq ($(findstring gcc,$(notdir $(CC))),)
# GCC のバージョン
GCC_VERSION_MAJOR	:= $(shell $(CC) -dumpversion | cut -d. -f1)
endif

# CC が Clang であった場合
ifneq ($(findstring clang,$(notdir $(CC))),)
# Clang のバージョン
CLANG_VERSION_MAJOR	:= $(shell $(CC) --version | awk '/clang version/ {match($$0, /[0-9]+\.[0-9]+\.[0-9]+/, a); print a[0]}' | cut -d. -f1)
endif

# MODE: 通常は空か 'release'、デバッグ時は 'debug'
MODE				?=

# LIB_MODE: DEBUG マクロをコンパイル時に定義するかどうか
# 定義しない場合は空か 'release'、定義する場合は 'debug'
LIB_MODE			?=

# 依存ライブラリ（この Makefile は POSIX互換 環境にしか対応していません）
CFLAGS				= -pthread -I. -I./libs
LDLIBS				= -pthread

# FORTIFY_SOURCE の値を gcc >= 12 または clang なら 3 、そうでなければ 2 に指定する
ifeq ($(shell (( [ $(findstring gcc,$(notdir $(CC))) ] && [ $(GCC_VERSION_MAJOR) -ge 12 ] ) || \
				[ $(findstring clang,$(notdir $(CC))) ] ) && echo yes),yes)
FORTIFY_LEVEL		:= 3
else
FORTIFY_LEVEL		:= 2
endif

# 共通のフラグ
COMMON_FLAGS		= -MMD -fstack-protector-strong -D_FORTIFY_SOURCE=$(FORTIFY_LEVEL) \
					-fstack-clash-protection -std=gnu17 -Wall -Wextra


# FCFチェック結果を保存するファイル名
CHECK_FCF_CACHE		:= .fcf_check_cache

# FCFチェック用関数（テストコンパイル）
define check_fcf_protection
	echo "int main() {return 0;}" | $(CC) -xc - -o /dev/null -fcf-protection=full 2>/dev/null
endef

# FCFチェック結果を読み込み、なければ実行してキャッシュに保存
ifeq ($(wildcard $(CHECK_FCF_CACHE)),)
CHECK_FCF			= $(shell if $(check_fcf_protection); then echo "yes" > $(CHECK_FCF_CACHE); else echo "no" > $(CHECK_FCF_CACHE); fi && echo yes)
else
CHECK_FCF			= yes
endif

# FCFチェック結果が yes なら -fcf-protection=full を追加
ifeq ($(shell echo $(CHECK_FCF) > /dev/null && cat $(CHECK_FCF_CACHE)),yes)
COMMON_FLAGS		+= -fcf-protection=full
endif


# -mbranch-protection=standard のチェック結果を保存するファイル名
CHECK_MBPS_CACHE	:= .mbps_check_cache

# -mbranch-protection=standard のチェック用関数（テストコンパイル）
define check_mbps_protection
	echo "int main() {return 0;}" | $(CC) -xc - -o /dev/null -mbranch-protection=standard 2>/dev/null
endef

# -mbranch-protection=standard のチェック結果を読み込み、なければ実行してキャッシュに保存
ifeq ($(wildcard $(CHECK_MBPS_CACHE)),)
CHECK_MBPS			= $(shell if $(check_mbps_protection); then echo "yes" > $(CHECK_MBPS_CACHE); else echo "no" > $(CHECK_MBPS_CACHE); fi && echo yes)
else
CHECK_MBPS			= yes
endif

# -mbranch-protection=standard のチェック結果が yes なら追加
ifeq ($(shell echo $(CHECK_MBPS) > /dev/null && cat $(CHECK_MBPS_CACHE)),yes)
COMMON_FLAGS		+= -mbranch-protection=standard
endif


# 最適化レベル（通常ビルド用）
OPT_FLAGS			= -O2 -DNDEBUG

# デバッグ用フラグ（debugターゲットなどで上書き）
DEBUG_FLAGS			= -O0 -g


# CC が GCC であった場合
ifneq ($(findstring gcc,$(notdir $(CC))),)

# 追加の警告フラグ（debugターゲットなどで上書き）
ADDITIONAL_FLAGS	= -Werror -Wmissing-declarations -Wmissing-include-dirs \
					-Wmissing-prototypes -Wstrict-prototypes -Wold-style-definition \
					-Wimplicit-function-declaration -Wmissing-field-initializers \
					-Wundef -Wbad-function-cast -Wdangling-else -Wtrampolines \
					-Wendif-labels -Wcomment -Wconversion -Wsign-conversion \
					-Wfloat-equal -Wmaybe-uninitialized -Wcast-align -Wcast-qual \
					-Wcast-function-type -Wcast-align=strict -Wfloat-conversion \
					-Wdouble-promotion -Wunsafe-loop-optimizations -Wpointer-arith \
					-Winit-self -Walloca -Walloc-zero -Wstringop-overflow \
					-Wstack-protector -Wformat=2 -Wformat-zero-length \
					-Wformat-signedness -Wformat-overflow=2 -Wformat-truncation=2 \
					-Wwrite-strings -Wvariadic-macros -Woverlength-strings -Wlogical-op \
					-Wswitch-default -Wduplicated-cond -Wduplicated-branches \
					-Wjump-misses-init -Wunreachable-code -Wnull-dereference \
					-Wattribute-alias=2 -Wshadow -Wredundant-decls -Wnested-externs \
					-Wdisabled-optimization -Wunsuffixed-float-constants \
					-Wunused-result -Wunused-macros -Wunused-local-typedefs -Wtrigraphs \
					-Wstrict-aliasing=2 -Wstrict-overflow=2 -Wframe-larger-than=10240 \
					-Wstack-usage=10240

# gcc 10 以上なら以下のオプションを追加
ifeq ($(shell [ $(GCC_VERSION_MAJOR) -ge 10 ] && echo yes),yes)
ADDITIONAL_FLAGS	+= -Warith-conversion -fanalyzer -fanalyzer-verbosity=3 \
					-fanalyzer-transitivity
# 問題が起きやすいオプションは分離
STRICT_FLAGS		= -Wanalyzer-too-complex -Wanalyzer-symbol-too-complex
endif

# gcc 10 なら以下のオプションを追加
ifeq ($(shell [ $(GCC_VERSION_MAJOR) -eq 10 ] && echo yes),yes)
ADDITIONAL_FLAGS	+= -Wno-analyzer-malloc-leak -Wno-analyzer-null-dereference
endif

# gcc 11 以上なら以下のオプションを追加
ifeq ($(shell [ $(GCC_VERSION_MAJOR) -ge 11 ] && echo yes),yes)
ADDITIONAL_FLAGS	+= -Warray-parameter
endif

# gcc 12 以上なら以下のオプションを追加
ifeq ($(shell [ $(GCC_VERSION_MAJOR) -ge 12 ] && echo yes),yes)
ADDITIONAL_FLAGS	+= -Wdangling-pointer=2 -Wbidi-chars=any,ucn
endif

# gcc 13 以上なら以下のオプションを追加
ifeq ($(shell [ $(GCC_VERSION_MAJOR) -ge 13 ] && echo yes),yes)
COMMON_FLAGS		+= -fstrict-flex-arrays=3
endif

# gcc 14 以上なら以下のオプションを追加
ifeq ($(shell [ $(GCC_VERSION_MAJOR) -ge 14 ] && echo yes),yes)
ADDITIONAL_FLAGS	+= -Walloc-size -Wcalloc-transposed-args -Wuseless-cast
endif

# gcc 14 なら以下のオプションを追加
ifeq ($(shell [ $(GCC_VERSION_MAJOR) -eq 14 ] && echo yes),yes)
ADDITIONAL_FLAGS	+= -Wflex-array-member-not-at-end
endif

# gcc 15 以上なら以下のオプションを追加
ifeq ($(shell [ $(GCC_VERSION_MAJOR) -ge 15 ] && echo yes),yes)
ADDITIONAL_FLAGS	+= -Wdeprecated-non-prototype -Wmissing-parameter-name \
					-Wstrict-flex-arrays=3 -Wfree-labels
endif

endif  # 93行目からここまで GCC のみ


SCAN_BUILD			=

# CC が Clang であった場合
ifneq ($(findstring clang,$(notdir $(CC))),)

# 追加の警告フラグ（debugターゲットなどで上書き）
ADDITIONAL_FLAGS	= -Werror -Wmissing-declarations -Wmissing-include-dirs \
					-Wmissing-prototypes -Wstrict-prototypes -Wold-style-definition \
					-Wimplicit-function-declaration -Wmissing-field-initializers \
					-Wundef -Wbad-function-cast -Wdangling-else -Wendif-labels -Wcomma \
					-Wcomment -Wconversion -Wsign-conversion -Wfloat-equal \
					-Wsign-compare -Wuninitialized -Wconditional-uninitialized \
					-Wcast-align -Wcast-qual -Wcast-function-type -Wcast-align \
					-Wfloat-conversion -Wdouble-promotion -Wloop-analysis \
					-Wfor-loop-analysis -Wunreachable-code-loop-increment \
					-Wpointer-arith -Winit-self -Walloca -Wstrlcpy-strlcat-size \
					-Warray-bounds -Wstack-protector -Wformat=2 -Wformat-zero-length \
					-Wwrite-strings -Wvariadic-macros -Woverlength-strings \
					-Wconstant-logical-operand -Wtautological-constant-in-range-compare \
					-Wlogical-not-parentheses -Wswitch-default -Wunreachable-code \
					-Wnull-dereference -Wshadow-all -Wredundant-decls -Wnested-externs \
					-Wdisabled-optimization -Wunused-result -Wunused-macros \
					-Wunused-local-typedefs -Wunused-label -Wtrigraphs -Wextra-semi \
					-Wstrict-aliasing=2 -Wstrict-overflow=2 -Wframe-larger-than=10240 \
					-Wmemset-transposed-args -Wgnu-array-member-paren-init

# MODE が debug であれば SCAN_BUILD を設定する
ifeq ($(MODE),debug)
ifneq (,$(filter clang-%,$(CC)))  # clang-XX とバージョンが指定されている場合は、scan-buildも同じバージョンを使う
SCAN_BUILD			= scan-build-$(subst clang-,,$(CC)) 
else
SCAN_BUILD			= scan-build 
endif
endif

# clang 15 以上なら以下のオプションを追加
ifeq ($(shell [ $(CLANG_VERSION_MAJOR) -ge 15 ] && echo yes),yes)
ADDITIONAL_FLAGS	+=  -Warray-parameter -Wdeprecated-non-prototype
endif

# clang 16 以上なら以下のオプションを追加
ifeq ($(shell [ $(CLANG_VERSION_MAJOR) -ge 16 ] && echo yes),yes)
COMMON_FLAGS		+= -fstrict-flex-arrays=3
endif

# clang 18 以上なら以下のオプションを追加
ifeq ($(shell [ $(CLANG_VERSION_MAJOR) -ge 18 ] && echo yes),yes)
ADDITIONAL_FLAGS	+= -Wformat-overflow -Wformat-truncation
endif

# clang 19 以上なら以下のオプションを追加
ifeq ($(shell [ $(CLANG_VERSION_MAJOR) -ge 19 ] && echo yes),yes)
ADDITIONAL_FLAGS	+= -Wformat-signedness
endif

endif  # 166行目からここまで Clang のみ


# LIB_MODE に応じて CFLAGS で DEBUG マクロを定義する
ifeq ($(LIB_MODE),debug)
CFLAGS				+= -DDEBUG
endif

# MODE に応じて CFLAGS を設定する
ifeq ($(MODE),debug)
CFLAGS				+= $(COMMON_FLAGS) $(DEBUG_FLAGS) $(ADDITIONAL_FLAGS)
else
CFLAGS				+= $(COMMON_FLAGS) $(OPT_FLAGS)
endif

# リンカフラグ
LDFLAGS				=

# ソースファイル
SRCS				= mhashtable.c

# オブジェクトファイル
OBJS				= $(SRCS:.c=.o)

# PIC対応のオブジェクトファイル
PIC_OBJS			= $(SRCS:.c=.pic.o)

# 依存ファイル
DEPS				= $(OBJS:.o=.d)
PIC_DEPS			= $(PIC_OBJS:.pic.o=.pic.d)

# 実行ファイル名
TARGET				=

# 静的ライブラリ名
STATIC_LIB			= libmhashtable.a

# 共有ライブラリ名
SHARED_LIB			= libmhashtable.so


# デバッグ時は事前にクリーン
ifeq ($(MODE),debug)
prebuild: clean all
endif


# デフォルトターゲット
DEFAULT_TARGET		?=

ifeq ($(DEFAULT_TARGET),)	# DEFAULT_TARGET (execfile or staticlib or sharedlib) が指定されていない場合
ifneq ($(TARGET),)				# 実行ファイル名がある場合
DEFAULT_TARGET		= execfile
else							# 実行ファイル名がない場合
ifneq ($(STATIC_LIB),)				# 静的ライブラリ名がある場合
DEFAULT_TARGET		= staticlib
else								# 静的ライブラリ名がない場合
DEFAULT_TARGET		= sharedlib
endif
endif
endif

all: $(DEFAULT_TARGET)


# 実行ファイルのターゲット
execfile: $(TARGET)

# 実行ファイルのビルド
$(TARGET): $(OBJS)
	$(CC) $(LDLIBS) -pie -o $@ $^


# 静的ライブラリのターゲット
staticlib: $(STATIC_LIB)

# 静的ライブラリのビルド
$(STATIC_LIB): $(PIC_OBJS)
	$(AR) rcs $@ $^


# 共有ライブラリのターゲット
sharedlib: $(SHARED_LIB)

# 共有ライブラリのビルド
$(SHARED_LIB): $(PIC_OBJS)
	$(CC) $(LDLIBS) -shared -o $@ $^


# オブジェクトファイルのビルド
%.o: %.c
	$(SCAN_BUILD)$(CC) $(CFLAGS) -fPIE -c $< -o $@


# PIC対応のオブジェクトファイルのビルド
%.pic.o: %.c
	$(SCAN_BUILD)$(CC) $(CFLAGS) -fPIC -c $< -o $@


# 依存関係ファイルの読み込み
-include $(DEPS)
-include $(PIC_DEPS)


ifneq ($(TARGET),)	# 実行ファイル名がある場合
# 実行
run:
	./$(TARGET)
endif


# クリーン
clean:
	rm -f $(TARGET) $(OBJS) $(DEPS) $(STATIC_LIB) $(SHARED_LIB) $(PIC_OBJS) $(PIC_DEPS)


# クリーンしてからビルド
firstrelease: clean all


# ファイルとは無関係なターゲット
.PHONY: prebuild all execfile staticlib sharedlib run clean firstrelease
