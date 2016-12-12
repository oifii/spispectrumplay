include ..\makefile.in

TARGET = spispectrumplay.exe
FLAGS += -mwindows
LIBS = -lcomdlg32 -lgdi32 -lwinmm

all: $(TARGET)

clean:
	$(RM) $(OUTDIR)\$(TARGET)
