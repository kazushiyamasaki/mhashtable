# コンパイラ
CC					= gcc

# GCCのバージョン
GCC_VERSION_MAJOR	:= $(shell $(CC) -dumpversion | cut -d. -f1)

# MODE: 通常は空か 'release'。デバッグ時は 'debug'、エラーが出すぎる場合は 'lightdbg'
MODE				?=

# 依存ライブラリ
LDLIBS				= -pthread

# 共通のフラグ（警告や依存関係生成）
COMMON_FLAGS		= -Wall -Wextra -MMD

# 最適化レベル（通常ビルド用）
OPT_FLAGS			= -O2

# デバッグ用フラグ（debugターゲットなどで上書き）
DEBUG_FLAGS			= -O0 -g

# 追加の警告フラグ（debugターゲットなどで上書き）
ADDITIONAL_FLAGS	= -Werror -Wmissing-declarations -Wmissing-include-dirs \
					-Wmissing-prototypes -Wstrict-prototypes -Wold-style-definition \
					-Wimplicit-function-declaration -Wmissing-field-initializers \
					-Wundef -Wbad-function-cast -Wcomment -Wcast-align -Wcast-qual \
					-Wcast-function-type -Wdouble-promotion -Wpointer-arith -Winit-self \
					-Walloca -fstack-protector-strong -Wstack-protector \
					-Wformat-security -Wwrite-strings -Wvariadic-macros \
					-Woverlength-strings -Wlogical-op -Wswitch-default \
					-Wduplicated-cond -Wduplicated-branches -Wjump-misses-init \
					-Wunreachable-code -Wnull-dereference -Wredundant-decls \
					-Wunused-parameter -Wnested-externs -Wdisabled-optimization \
					-Wunsuffixed-float-constants -Wstrict-aliasing=2 \

# 偽陽性が起こりやすいフラグは分離しておく
STRICT_FLAGS		= -Wconversion -Wsign-conversion -Wfloat-equal -Wshadow \
					-Wstrict-overflow=2

# gcc <= 6 なら ADDITIONAL_FLAGS に暗黙の変換による精度低下の警告を追加
ifeq ($(shell [ $(GCC_VERSION_MAJOR) -ge 6 ] && echo yes),yes)
	ADDITIONAL_FLAGS	+= -Wfloat-conversion
endif

# gcc <= 7 なら ADDITIONAL_FLAGS に以下の3つを追加
ifeq ($(shell [ $(GCC_VERSION_MAJOR) -ge 7 ] && echo yes),yes)
	ADDITIONAL_FLAGS	+= -Wcast-align=strict -Wformat-overflow=2 -Wformat-truncation=2
endif

# gcc <= 8 なら ADDITIONAL_FLAGS にスタック保護機能のオプションを追加
ifeq ($(shell [ $(GCC_VERSION_MAJOR) -ge 8 ] && echo yes),yes)
	ADDITIONAL_FLAGS	+= -fstack-clash-protection
endif

# gcc <= 10 なら STRICT_FLAGS に静的解析を追加
ifeq ($(shell [ $(GCC_VERSION_MAJOR) -ge 10 ] && echo yes),yes)
	STRICT_FLAGS		+= -fanalyzer
endif

# MODE に応じて CFLAGS を設定する
ifeq ($(MODE),debug)
	CFLAGS = $(COMMON_FLAGS) $(DEBUG_FLAGS) $(ADDITIONAL_FLAGS) $(STRICT_FLAGS)
else ifeq ($(MODE),lightdbg)
	CFLAGS = $(COMMON_FLAGS) $(DEBUG_FLAGS) $(ADDITIONAL_FLAGS)
else
	CFLAGS = $(COMMON_FLAGS) $(OPT_FLAGS)
endif

# リンカフラグ
LDFLAGS				=

# ソースファイル
SRCS				= mhashtable.c

# オブジェクトファイル
OBJS				= $(SRCS:.c=.o)

# 依存ファイル
DEPS				= $(OBJS:.o=.d)
PIC_DEPS			= $(PIC_OBJS:.pic.o=.pic.d)

# 実行ファイル名
TARGET				=

# 静的ライブラリ名
STATIC_LIB			= libmhashtable.a

# 共有ライブラリ名
SHARED_LIB			= libmhashtable.so

# PIC対応のオブジェクトファイル
PIC_OBJS			= $(SRCS:.c=.pic.o)


# デバッグ時は事前にクリーン
ifeq ($(filter $(MODE),debug lightdbg),debug lightdbg)
all: prebuild
prebuild: clean
endif


# デフォルトターゲット
ifeq ($(TARGET),)	# 実行ファイル名がない場合は静的ライブラリをターゲットに
	ifneq ($(STATIC_LIB),)
all: staticlib
	else			# 静的ライブラリ名もない場合は共有ライブラリをターゲットに
all: sharedlib
	endif
else				# 通常
all: $(TARGET)

# 実行ファイルのビルド
$(TARGET): $(OBJS)
	$(CC) $(LDFLAGS) $(LDLIBS) -o $@ $^
endif


# 静的ライブラリのターゲット
staticlib: $(STATIC_LIB)

# 静的ライブラリのビルド
$(STATIC_LIB): $(OBJS)
	ar rcs $@ $^


# 共有ライブラリのターゲット
sharedlib: $(SHARED_LIB)

# 共有ライブラリのビルド
$(SHARED_LIB): $(PIC_OBJS)
	$(CC) -shared -o $@ $^ $(LDLIBS)

# PIC対応のオブジェクトファイルのビルド
%.pic.o: %.c
	$(CC) $(CFLAGS) $(LDLIBS) -fPIC -c $< -o $@


# オブジェクトファイルのビルド
%.o: %.c
	$(CC) $(CFLAGS) $(LDLIBS) -c $< -o $@


# 依存関係ファイルの読み込み
-include $(DEPS)
-include $(PIC_DEPS)


# クリーン
clean:
	rm -f $(TARGET) $(OBJS) $(DEPS) $(STATIC_LIB) $(SHARED_LIB) $(PIC_OBJS) $(PIC_DEPS)


# ファイルとは無関係なターゲット
.PHONY: all prebuild staticlib sharedlib clean
