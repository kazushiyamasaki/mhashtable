# コンパイラ（この Makefile は GCC 9 以上にしか対応していません）
CC					= gcc

# GCCのバージョン
GCC_VERSION_MAJOR	:= $(shell $(CC) -dumpversion | cut -d. -f1)

# MODE: 通常は空か 'release'、デバッグ時は 'debug'
MODE				?=

# 依存ライブラリ
LDLIBS				= -pthread

# FORTIFY_SOURCE の値を gcc >= 12 なら 3 、そうでなければ 2 に指定する
ifeq ($(shell [ $(GCC_VERSION_MAJOR) -ge 12 ] && echo yes),yes)
FORTIFY_LEVEL		= 3
else
FORTIFY_LEVEL		= 2
endif

# 共通のフラグ
COMMON_FLAGS		= -MMD -fstack-protector-strong -D_FORTIFY_SOURCE=$(FORTIFY_LEVEL) \
					-std=gnu17 -Wall -Wextra

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

# FCFチェック結果によってはオプションを追加
ifeq ($(shell echo $(CHECK_FCF) > /dev/null && cat $(CHECK_FCF_CACHE)),yes)
COMMON_FLAGS		+= -fcf-protection=full
endif

# 最適化レベル（通常ビルド用）
OPT_FLAGS			= -O2

# デバッグ用フラグ（debugターゲットなどで上書き）
DEBUG_FLAGS			= -O0 -g

# 追加の警告フラグ（debugターゲットなどで上書き）
ADDITIONAL_FLAGS	= -Wmissing-declarations -Wmissing-include-dirs \
					-Wmissing-prototypes -Wstrict-prototypes -Wold-style-definition \
					-Wimplicit-function-declaration -Wmissing-field-initializers \
					-Wundef -Wbad-function-cast -Wdangling-else -Wtrampolines -Wcomment \
					-Wconversion -Wsign-conversion -Wfloat-equal -Wmaybe-uninitialized \
					-Wcast-align -Wcast-qual -Wcast-function-type -Wcast-align=strict \
					-Wfloat-conversion -Wdouble-promotion -Wunsafe-loop-optimizations \
					-Wpointer-arith -Winit-self -Walloca -Walloc-zero \
					-Wstringop-overflow -fstack-protector-strong -Wstack-protector \
					-fstack-clash-protection -Wformat=2 -Wformat-zero-length \
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
ADDITIONAL_FLAGS	+= -Wdangling-pointer=2 -Wbidi-chars=ucn
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

# 問題が起きやすいオプションは分離
STRICT_FLAGS	= -Wanalyzer-too-complex -Wanalyzer-symbol-too-complex

# MODE に応じて CFLAGS を設定する
ifeq ($(MODE),debug)
CFLAGS				= $(COMMON_FLAGS) $(DEBUG_FLAGS) $(ADDITIONAL_FLAGS)
else
CFLAGS				= $(COMMON_FLAGS) $(OPT_FLAGS)
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
	ar rcs $@ $^


# 共有ライブラリのターゲット
sharedlib: $(SHARED_LIB)

# 共有ライブラリのビルド
$(SHARED_LIB): $(PIC_OBJS)
	$(CC) $(LDLIBS) -shared -o $@ $^


# オブジェクトファイルのビルド
%.o: %.c
	$(CC) $(CFLAGS) $(LDLIBS) -fPIE -c $< -o $@


# PIC対応のオブジェクトファイルのビルド
%.pic.o: %.c
	$(CC) $(CFLAGS) $(LDLIBS) -fPIC -c $< -o $@


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
