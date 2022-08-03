
SDL_CFLAGS := `sdl-config --cflags`
SDL_LIBS   := `sdl-config --libs`

MODPLUG_LIBS ?= -lmodplug

BB := decode.c game.c level.c objects.c resource.c screen.c sound.c staticres.c tiles.c unpack.c
JA := game.c level.c resource.c screen.c sound.c staticres.c unpack.c
P2 := bosses.c game.c level.c monsters.c resource.c screen.c sound.c staticres.c unpack.c

BB_SRCS := $(foreach f,$(BB),bb/$f)
JA_SRCS := $(foreach f,$(JA),ja/$f)
P2_SRCS := $(foreach f,$(P2),p2/$f)
SRCS := $(BB_SRCS) $(JA_SRCS) $(P2_SRCS)
OBJS := $(SRCS:.c=.o)
DEPS := $(SRCS:.c=.d)

CPPFLAGS += -Wall -Wpedantic -Wno-unused-function -D_GNU_SOURCE -MMD $(SDL_CFLAGS) -I. -g
ifdef X11
CFLAGS += -DHAVE_X11=yes
LDFLAGS += -lX11 -lXrandr
endif

all: blues bbja pre2

blues: main.o sys.o util.o $(BB_SRCS:.c=.o)
	$(CC) $(LDFLAGS) -o $@ $^ $(SDL_LIBS) $(MODPLUG_LIBS)

bbja: main.o sys.o util.o $(JA_SRCS:.c=.o)
	$(CC) $(LDFLAGS) -o $@ $^ $(SDL_LIBS) $(MODPLUG_LIBS)

pre2: main.o sys.o sys_sine.o util.o $(P2_SRCS:.c=.o)
	$(CC) $(LDFLAGS) -o $@ $^ $(SDL_LIBS) $(MODPLUG_LIBS)

clean:
	rm -f $(OBJS) $(DEPS) *.o *.d

-include $(DEPS)
