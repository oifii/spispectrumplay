/*
 * Copyright (c) 2010-2016 Stephane Poirier
 *
 * stephane.poirier@oifii.org
 *
 * Stephane Poirier
 * 3532 rue Ste-Famille, #3
 * Montreal, QC, H2X 2L1
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include <windows.h>
#include <stdio.h>
#include <math.h>
#include <malloc.h>
#include "bass.h"

#include "resource.h"

#include <string>
using namespace std;

#define SPECWIDTH 368	// display width
#define SPECHEIGHT 127	// height (changing requires palette adjustments too)

BYTE global_alpha=200;

char global_buffer[1024];
string global_filename="testwav.wav";
float global_fSecondsPlay; //negative for playing only once
DWORD global_timer=0;
int global_x=200;
int global_y=200;

HWND win=NULL;
DWORD timer=0;

DWORD chan;

HDC specdc=0;
HBITMAP specbmp=0;
BYTE *specbuf;

int specmode=0,specpos=0; // spectrum mode (and marker pos for 2nd mode)


void CALLBACK StopPlayingFile(UINT uTimerID, UINT uMsg, DWORD dwUser, DWORD dw1, DWORD dw2)
{
	PostMessage(win, WM_DESTROY, 0, 0);
}

// display error messages
void Error(const char *es)
{
	char mes[200];
	sprintf(mes,"%s\n(error code: %d)",es,BASS_ErrorGetCode());
	MessageBox(win,mes,0,0);
}

BOOL PlayFile(const char* filename)
{
	if (!(chan=BASS_StreamCreateFile(FALSE,filename,0,0,BASS_SAMPLE_LOOP))
		&& !(chan=BASS_MusicLoad(FALSE,filename,0,0,BASS_MUSIC_RAMP|BASS_SAMPLE_LOOP,1))) 
	{
		Error("Can't play file");
		return FALSE; // Can't load the file
	}
	if(global_fSecondsPlay<=0)
	{
		QWORD length_byte=BASS_ChannelGetLength(chan,BASS_POS_BYTE);
		global_fSecondsPlay=BASS_ChannelBytes2Seconds(chan,length_byte);
	}
	global_timer=timeSetEvent(global_fSecondsPlay*1000,25,(LPTIMECALLBACK)&StopPlayingFile,0,TIME_ONESHOT);

	BASS_ChannelPlay(chan,FALSE);
	return TRUE;
}

// select a file to play, and play it
BOOL PlayFile()
{
	char file[MAX_PATH]="";
	OPENFILENAME ofn={0};
	ofn.lStructSize=sizeof(ofn);
	ofn.hwndOwner=win;
	ofn.nMaxFile=MAX_PATH;
	ofn.lpstrFile=file;
	ofn.Flags=OFN_FILEMUSTEXIST|OFN_HIDEREADONLY|OFN_EXPLORER;
	ofn.lpstrTitle="Select a file to play";
	ofn.lpstrFilter="playable files\0*.mo3;*.xm;*.mod;*.s3m;*.it;*.mtm;*.umx;*.mp3;*.mp2;*.mp1;*.ogg;*.wav;*.aif\0All files\0*.*\0\0";
	if (!GetOpenFileName(&ofn)) return FALSE;
	
	return PlayFile(file);
}

// update the spectrum display - the interesting bit :)
void CALLBACK UpdateSpectrum(UINT uTimerID, UINT uMsg, DWORD dwUser, DWORD dw1, DWORD dw2)
{
	HDC dc;
	int x,y,y1;

	if (specmode==3) 
	{ // waveform
		int c;
		float *buf;
		BASS_CHANNELINFO ci;
		memset(specbuf,0,SPECWIDTH*SPECHEIGHT);
		BASS_ChannelGetInfo(chan,&ci); // get number of channels
		//buf=alloca(ci.chans*SPECWIDTH*sizeof(float)); // allocate buffer for data
		buf=(float*)alloca(ci.chans*SPECWIDTH*sizeof(float)); // allocate buffer for data
		BASS_ChannelGetData(chan,buf,(ci.chans*SPECWIDTH*sizeof(float))|BASS_DATA_FLOAT); // get the sample data (floating-point to avoid 8 & 16 bit processing)
		for (c=0;c<ci.chans;c++) 
		{
			for (x=0;x<SPECWIDTH;x++) 
			{
				int v=(1-buf[x*ci.chans+c])*SPECHEIGHT/2; // invert and scale to fit display
				if (v<0) v=0;
				else if (v>=SPECHEIGHT) v=SPECHEIGHT-1;
				if (!x) y=v;
				do 
				{ // draw line from previous sample...
					if (y<v) y++;
					else if (y>v) y--;
					specbuf[y*SPECWIDTH+x]=c&1?127:1; // left=green, right=red (could add more colours to palette for more chans)
				} while (y!=v);
			}
		}
	} 
	else 
	{
		float fft[1024];
		BASS_ChannelGetData(chan,fft,BASS_DATA_FFT2048); // get the FFT data

		if (!specmode) 
		{ // "normal" FFT
			memset(specbuf,0,SPECWIDTH*SPECHEIGHT);
			for (x=0;x<SPECWIDTH/2;x++) 
			{
#if 1
				y=sqrt(fft[x+1])*3*SPECHEIGHT-4; // scale it (sqrt to make low values more visible)
#else
				y=fft[x+1]*10*SPECHEIGHT; // scale it (linearly)
#endif
				if (y>SPECHEIGHT) y=SPECHEIGHT; // cap it
				if (x && (y1=(y+y1)/2)) // interpolate from previous to make the display smoother
					while (--y1>=0) specbuf[y1*SPECWIDTH+x*2-1]=y1+1;
				y1=y;
				while (--y>=0) specbuf[y*SPECWIDTH+x*2]=y+1; // draw level
			}
		} 
		else if (specmode==1) 
		{ // logarithmic, acumulate & average bins
			int b0=0;
			memset(specbuf,0,SPECWIDTH*SPECHEIGHT);
//#define BANDS 28
//#define BANDS 40
#define BANDS 12
			for (x=0;x<BANDS;x++) 
			{
				float peak=0;
				int b1=pow(2,x*10.0/(BANDS-1));
				if (b1>1023) b1=1023;
				if (b1<=b0) b1=b0+1; // make sure it uses at least 1 FFT bin
				for (;b0<b1;b0++)
					if (peak<fft[1+b0]) peak=fft[1+b0];
				y=sqrt(peak)*3*SPECHEIGHT-4; // scale it (sqrt to make low values more visible)
				if (y>SPECHEIGHT) y=SPECHEIGHT; // cap it
				while (--y>=0)
					memset(specbuf+y*SPECWIDTH+x*(SPECWIDTH/BANDS),y+1,SPECWIDTH/BANDS-2); // draw bar
			}
		} 
		else 
		{ // "3D"
			for (x=0;x<SPECHEIGHT;x++) 
			{
				y=sqrt(fft[x+1])*3*127; // scale it (sqrt to make low values more visible)
				if (y>127) y=127; // cap it
				specbuf[x*SPECWIDTH+specpos]=128+y; // plot it
			}
			// move marker onto next position
			specpos=(specpos+1)%SPECWIDTH;
			for (x=0;x<SPECHEIGHT;x++) specbuf[x*SPECWIDTH+specpos]=255;
		}
	}

	// update the display
	dc=GetDC(win);
	BitBlt(dc,0,0,SPECWIDTH,SPECHEIGHT,specdc,0,0,SRCCOPY);
	ReleaseDC(win,dc);
}

// window procedure
long FAR PASCAL SpectrumWindowProc(HWND h, UINT m, WPARAM w, LPARAM l)
{
	switch (m) {
		case WM_PAINT:
			if (GetUpdateRect(h,0,0)) {
				PAINTSTRUCT p;
				HDC dc;
				if (!(dc=BeginPaint(h,&p))) return 0;
				BitBlt(dc,0,0,SPECWIDTH,SPECHEIGHT,specdc,0,0,SRCCOPY);
				EndPaint(h,&p);
			}
			return 0;

		case WM_LBUTTONUP:
			specmode=(specmode+1)%4; // swap spectrum mode
			memset(specbuf,0,SPECWIDTH*SPECHEIGHT);	// clear display
			return 0;

		case WM_CREATE:
			win=h;
			//spi, avril 2015, begin
			SetWindowLong(h, GWL_EXSTYLE, GetWindowLong(h, GWL_EXSTYLE) | WS_EX_LAYERED);
			SetLayeredWindowAttributes(h, 0, global_alpha, LWA_ALPHA);
			//SetLayeredWindowAttributes(h, 0, 200, LWA_ALPHA);
			//spi, avril 2015, end
			// initialize BASS
			if (!BASS_Init(-1,44100,0,win,NULL)) {
				Error("Can't initialize device");
				return -1;
			}
			//if (!PlayFile()) { // start a file playing
			if (!PlayFile(global_filename.c_str())) { // start a file playing
				BASS_Free();
				return -1;
			}
			{ // create bitmap to draw spectrum in (8 bit for easy updating)
				BYTE data[2000]={0};
				BITMAPINFOHEADER *bh=(BITMAPINFOHEADER*)data;
				RGBQUAD *pal=(RGBQUAD*)(data+sizeof(*bh));
				int a;
				bh->biSize=sizeof(*bh);
				bh->biWidth=SPECWIDTH;
				bh->biHeight=SPECHEIGHT; // upside down (line 0=bottom)
				bh->biPlanes=1;
				bh->biBitCount=8;
				bh->biClrUsed=bh->biClrImportant=256;
				// setup palette
				
				//original palette, green shifting to red
				for (a=1;a<128;a++) {
					pal[a].rgbGreen=256-2*a;
					pal[a].rgbRed=2*a;
				}
				for (a=0;a<32;a++) {
					pal[128+a].rgbBlue=8*a;
					pal[128+32+a].rgbBlue=255;
					pal[128+32+a].rgbRed=8*a;
					pal[128+64+a].rgbRed=255;
					pal[128+64+a].rgbBlue=8*(31-a);
					pal[128+64+a].rgbGreen=8*a;
					pal[128+96+a].rgbRed=255;
					pal[128+96+a].rgbGreen=255;
					pal[128+96+a].rgbBlue=8*a;
				}
				
				/*
				//altered palette, red shifting to green
				for (a=1;a<128;a++) {
					pal[a].rgbRed=256-2*a;
					pal[a].rgbGreen=2*a;
				}
				for (a=0;a<32;a++) {
					pal[128+a].rgbBlue=8*a;
					pal[128+32+a].rgbBlue=255;
					pal[128+32+a].rgbGreen=8*a;
					pal[128+64+a].rgbGreen=255;
					pal[128+64+a].rgbBlue=8*(31-a);
					pal[128+64+a].rgbRed=8*a;
					pal[128+96+a].rgbGreen=255;
					pal[128+96+a].rgbRed=255;
					pal[128+96+a].rgbBlue=8*a;
				}
				*/
				/*
				//altered palette, blue shifting to green
				for (a=1;a<128;a++) {
					pal[a].rgbBlue=256-2*a;
					pal[a].rgbGreen=2*a;
				}
				for (a=0;a<32;a++) {
					pal[128+a].rgbBlue=8*a;
					pal[128+32+a].rgbRed=255;
					pal[128+32+a].rgbGreen=8*a;
					pal[128+64+a].rgbGreen=255;
					pal[128+64+a].rgbRed=8*(31-a);
					pal[128+64+a].rgbBlue=8*a;
					pal[128+96+a].rgbGreen=255;
					pal[128+96+a].rgbBlue=255;
					pal[128+96+a].rgbRed=8*a;
				}
				*/
				/*
				//altered palette, black shifting to white - grascale
				for (a=1;a<256;a++) {
					pal[a].rgbRed=a;
					pal[a].rgbBlue=a;
					pal[a].rgbGreen=a;
				}
				*/
				// create the bitmap
				specbmp=CreateDIBSection(0,(BITMAPINFO*)bh,DIB_RGB_COLORS,(void**)&specbuf,NULL,0);
				specdc=CreateCompatibleDC(0);
				SelectObject(specdc,specbmp);
			}
			// setup update timer (40hz)
			timer=timeSetEvent(25,25,(LPTIMECALLBACK)&UpdateSpectrum,0,TIME_PERIODIC);
			break;

		case WM_DESTROY:
			{
				if (timer) timeKillEvent(timer);
				if (global_timer) timeKillEvent(global_timer);
				BASS_Free();
				if (specdc) DeleteDC(specdc);
				if (specbmp) DeleteObject(specbmp);

				int nShowCmd = false;
				ShellExecuteA(NULL, "open", "end.bat", "", NULL, nShowCmd);
				PostQuitMessage(0);
			}
			break;
	}
	return DefWindowProc(h, m, w, l);
}

PCHAR*
    CommandLineToArgvA(
        PCHAR CmdLine,
        int* _argc
        )
    {
        PCHAR* argv;
        PCHAR  _argv;
        ULONG   len;
        ULONG   argc;
        CHAR   a;
        ULONG   i, j;

        BOOLEAN  in_QM;
        BOOLEAN  in_TEXT;
        BOOLEAN  in_SPACE;

        len = strlen(CmdLine);
        i = ((len+2)/2)*sizeof(PVOID) + sizeof(PVOID);

        argv = (PCHAR*)GlobalAlloc(GMEM_FIXED,
            i + (len+2)*sizeof(CHAR));

        _argv = (PCHAR)(((PUCHAR)argv)+i);

        argc = 0;
        argv[argc] = _argv;
        in_QM = FALSE;
        in_TEXT = FALSE;
        in_SPACE = TRUE;
        i = 0;
        j = 0;

        while( a = CmdLine[i] ) {
            if(in_QM) {
                if(a == '\"') {
                    in_QM = FALSE;
                } else {
                    _argv[j] = a;
                    j++;
                }
            } else {
                switch(a) {
                case '\"':
                    in_QM = TRUE;
                    in_TEXT = TRUE;
                    if(in_SPACE) {
                        argv[argc] = _argv+j;
                        argc++;
                    }
                    in_SPACE = FALSE;
                    break;
                case ' ':
                case '\t':
                case '\n':
                case '\r':
                    if(in_TEXT) {
                        _argv[j] = '\0';
                        j++;
                    }
                    in_TEXT = FALSE;
                    in_SPACE = TRUE;
                    break;
                default:
                    in_TEXT = TRUE;
                    if(in_SPACE) {
                        argv[argc] = _argv+j;
                        argc++;
                    }
                    _argv[j] = a;
                    j++;
                    in_SPACE = FALSE;
                    break;
                }
            }
            i++;
        }
        _argv[j] = '\0';
        argv[argc] = NULL;

        (*_argc) = argc;
        return argv;
    }

int PASCAL WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,LPSTR lpCmdLine, int nCmdShow)
{
	int nShowCmd = false;
	ShellExecuteA(NULL, "open", "begin.bat", "", NULL, nShowCmd);

	//LPWSTR *szArgList;
	LPSTR *szArgList;
	int argCount;
	//szArgList = CommandLineToArgvW(GetCommandLineW(), &argCount);
	szArgList = CommandLineToArgvA(GetCommandLine(), &argCount);
	if (szArgList == NULL)
	{
		MessageBox(NULL, "Unable to parse command line", "Error", MB_OK);
		return 10;
	}
	//global_filename="testwav.wav";
	if(argCount>1)
	{
		global_filename = szArgList[1]; 
	
		/*
		int ret = wcstombs ( global_buffer, szArgList[1], sizeof(global_buffer) );
		if (ret==sizeof(global_buffer)) global_buffer[sizeof(global_buffer)-1]='\0';
		global_filename = global_buffer; 
		*/
	}
	
	global_fSecondsPlay = -1.0; //negative for playing only once
	//global_fSecondsPlay = 3600.0f; //dj-oifii
	if(argCount>2)
	{
		global_fSecondsPlay = atof((LPCSTR)(szArgList[2]));
		//global_fSecondsPlay = _wtof(szArgList[2]);
	}
	if(argCount>3)
	{
		global_x = atoi((LPCSTR)(szArgList[3]));
	}
	if(argCount>4)
	{
		global_y = atoi((LPCSTR)(szArgList[4]));
	}
	if(argCount>5)
	{
		specmode = atoi((LPCSTR)(szArgList[5]));
	}
	if(argCount>6)
	{
		global_alpha = atoi(szArgList[6]);
	}
	LocalFree(szArgList);


	WNDCLASS wc={0};
    MSG msg;

	// check the correct BASS was loaded
	if (HIWORD(BASS_GetVersion())!=BASSVERSION) {
		MessageBox(0,"An incorrect version of BASS.DLL was loaded",0,MB_ICONERROR);
		return 0;
	}

	// register window class and create the window
	wc.lpfnWndProc = SpectrumWindowProc;
	wc.hInstance = hInstance;
	wc.hCursor = LoadCursor(NULL, IDC_ARROW);
	wc.hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_ICON1)); //spi, added
	wc.lpszClassName = "BASS-Spectrum";
	if (!RegisterClass(&wc) || !CreateWindow("BASS-Spectrum",
			//"BASS spectrum example (click to toggle mode)",
			"spispectrumplay (click to toggle mode)",
			//WS_POPUPWINDOW|WS_VISIBLE, global_x, global_y,
			WS_POPUPWINDOW|WS_CAPTION|WS_VISIBLE, global_x, global_y,
			//WS_POPUPWINDOW|WS_VISIBLE, 200, 200,
			SPECWIDTH+2*GetSystemMetrics(SM_CXDLGFRAME),
			SPECHEIGHT+GetSystemMetrics(SM_CYCAPTION)+2*GetSystemMetrics(SM_CYDLGFRAME),
			//SPECHEIGHT,
			NULL, NULL, hInstance, NULL)) {
		Error("Can't create window");
		return 0;
	}
	/*
	//spi, avril 2015, begin
	SetWindowLong(win, GWL_EXSTYLE, GetWindowLong(win, GWL_EXSTYLE) | WS_EX_LAYERED);
	//SetLayeredWindowAttributes(h, 0, global_alpha, LWA_ALPHA);
	SetLayeredWindowAttributes(win, 0, 200, LWA_ALPHA);
	//spi, avril 2015, end
	*/
	ShowWindow(win, SW_SHOWNORMAL);

	while (GetMessage(&msg,NULL,0,0)>0) {
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}

	return 0;
}
