// All the stuff you need for Centigram's TruVoice engine
//
// A good chunk of this stuff came from Dialogic's TextTalk SDK from 1996.
//
// escape sequence commands and audio format stuff was found and added by me :)
//
// since TruVoice is the successor to Speech Plus's Prose system, most of these commands
// will work on the Prose as well. The ones that can be used with Prose will be noted.
//
// @rommix0

/*////////////////////////////////////////////////////////////////////////////////////////////


ESCAPE SEQUENCES:
----------------------------------------------------------------------------------------------

\x1B[{d}V		# voice (use defines for voices)
				# works for both TruVoice and Prose 2000 (yes, really!!, only 3 voices for Prose 2000)

\x1B[{d}a		# amplitude (volume. higher value is lower)
				# works for both TruVoice and Prose 2000
				# (0 < x < 16)

\x1B[{d}f		# fast read mode (0 < x < 9)
				# works for both TruVoice and Prose 2000

\x1B[{d}r		# speech rate (50 < x < 250)
				# works for both TruVoice and Prose 2000

\x1B[{d}p		# pitch (50 < x < 400)
				# works for both TruVoice and Prose 2000

\x1B[{d}P		# speaking mode (0 - word read, 1 - regular prosody)
				# works for both TruVoice and Prose 2000

\x1B[{d}X		# context mode (for reading special text like email messages)

\x1B[{d}c		# voice/character mode (0 - normal, 1 - whisper, 2 - monotone)

\x1B[{d}I		# text/phoneme mode (0 - text, 1 - phoneme)
				# works for both TruVoice and Prose 2000

\x1B[{d}s		# adds pause with number being the duration in hundredths of a second
				# works for both TruVoice and Prose 2000

\x1B[{d}t		# tests phonemes. only used by the developers for debugging the synthesizer.
				# works for both TruVoice and Prose 2000

\x1B[{d}v		# speech speed (0 < x < 25)
				# based on disassembly, the default value is 13.
				# similar to speech rate, but more focused on enunciation
				# works for both TruVoice and Prose 2000

\x1B[x			# mark the end of sentence manually
				# works for both TruVoice and Prose 2000

////////////////////////////////////////////////////////////////////////////////////////////*/

#ifndef CGRM_H
#define CGRM_H

// ERROR ENUMS
#define TTS_ERROR_START				-600
#define TTS_NOT_SPEAKING			-601
#define TTS_INVALID_RCU				-602
#define TTS_WPM_RANGE				-603
#define TTS_PITCH_RANGE				-604
#define TTS_VOLUME_RANGE			-605
#define TTS_NOT_IDLE				-606
#define TTS_BAD_FILENAME			-607
#define TTS_NULL_STRING				-608
#define TTS_NOT_PAUSED				-609
#define TTS_PAUSED					-610
#define TTS_NO_RESOURCES			-611
#define TTS_ENGINE_ERROR			-612
#define TTS_DONGLE_ERROR			-613
#define TTS_LICEN_ERROR				-614
#define TTS_EMAIL_MALLOC			-615
#define TTS_TRANS_EMAIL				-616
#define TTS_HI_PRI_HDLR				-617
#define TTS_SPEAKING				-618
#define TTS_NOT_INIT				-619
#define TTS_NOT_ENABLED				-620
#define TTS_DX_HANDLE				-621
#define TTS_DX_BUSY					-622
#define TTS_ERROR_END				-623
#define TTS_INVALIDHANDLE			-1000
#define TTS_WAVEOUTBUSY				-1001
#define TTS_MAXNUMOPENED			-1002
#define TTS_WAVEOUTBUFFAILED		-1003
#define TTS_WAVEOUTOPENFAILED		-1004
#define TTS_INVALIDSRATE			-1005
#define TTS_INVALIDSFORMAT			-1006
#define TTS_WAVEOUTPROBLEM			-1007
#define TTS_INVALIDFILEHANDLE		-1008
#define TTS_INVALIDHEADERFORMAT		-1009
#define TTS_WRITEERROR				-1010
#define TTS_INVALIDSETTING			-1011
#define TTS_INVALIDCALLBACKTYPE		-1012
#define TTS_INVALIDPARAM			-1013
#define TTS_OUTOFMEMORY				-1014
#define TTS_NOTWAVEOUTDEVICE		-1015
#define TTS_CREATETHREADFAILED		-1016
#define TTS_NOTENOUGHSPACEINBUFFER	-1017
#define TTS_NOTSAVEBUFFERMODE		-1018
#define TTS_BUFFERTOOSMALL			-1019
#define TTS_EVENBUFFERSIZENEEDED	-1020
#define TTS_NOTBUSY					-1021
#define TTS_GETDATABUSY				-1022
#define TTS_LASTDATABUFFER			-1023
#define TTS_JUMPINPROGRESS			-1024
#define TTS_FILEOPENERROR			-1025
#define TTS_FILETOOBIG				-1026

// speech rate range
#define MAX_SPEECH_RATE		250
#define MIN_SPEECH_RATE		50

// pitch range
#define MAX_PITCH			400
#define MIN_PITCH			50

// volume range (higher is lower)
#define MAX_VOLUME			16
#define MIN_VOLUME			0

// English Character Voices
#define PETER					0
#define SIDNEY					1
#define EDDIE					2
#define DOUGLAS					3
#define BIFF					4
#define AMOS					5
#define MELVIN					6
#define ALEX					7
#define WANDA					8
#define JULIA					9

// Defines for context mode functions
#define DEFAULT_CTX				0
#define EMAIL_CTX				1

// Defines for voice characteristic functions
#define NORMAL_CHR				0
#define WHISPER_CHR				1
#define MONOTONE_CHR			2

// Default settings
#define TTS_DEFAULT_SPEECH_RATE	150
#define TTS_DEFAULT_PITCH		85
#define TTS_DEFAULT_VOLUME		5
#define TTS_DEFAULT_VOICE		0

// Jump defines
#define TTS_JUMP_CHARACTER		0
#define TTS_JUMP_WORD			1
#define TTS_JUMP_SENTENCE		2
#define TTS_JUMP_BEGINNING		10
#define TTS_JUMP_END			11

// TTS options for tts_Open()
#define TTS_PCM					0
#define TTS_ALAW				0x00030000
#define TTS_8K					1
#define TTS_11K					0

// miscellaneous
#define TTS_EV_FILE_END			0x01053000
#define TTS_CALLBACK			1000

// audio file formats
#define WAVE_FILE				0			// regular WAVE file
#define RAW_FILE				1			// RAW PCM file (headerless)
#define AU_FILE					2			// Sun/NeXT AU file


// API function prototypes

// tts_SaveFile(int* tts_handle, HFILE file_handle, const char* text, int header_format);
typedef int (__cdecl *tts_SaveFileFunc)(void*, HFILE, const char*, int);

// tts_CallBack(int* tts_handle, int callback_type, HWND callback_handle_a, int callback_handle_b);
typedef int (__cdecl *tts_CallBackFunc)(void*, int, void*, int);

// tts_UserDic(int* tts_handle, HWND hWnd);
//typedef int (__cdecl *tts_UserDicFunc)(void*, HWND);

// tts_Open(int uDeviceID, int options, int*& tts_handle);
typedef int (__cdecl *tts_OpenFunc)(int, int, void*&);

// tts_Close(void* tts_handle);
typedef int (__cdecl *tts_CloseFunc)(void*);

// tts_Speak(int* tts_handle, const char* text);
typedef int (__cdecl *tts_SpeakFunc)(void*, const char*);

// tts_Phoneme(int* tts_handle, char* text_to_convert, char* phoneme_out, int text_length, int* phoneme_out_length);
typedef int (__cdecl *tts_PhonemeFunc)(void*, char*, char*, int, int*);

// tts_Pause(int* tts_handle);
//typedef int (__cdecl *tts_PauseFunc)(void*);

// tts_Reset(int* tts_handle);
//typedef int (__cdecl *tts_ResetFunc)(void*);

// tts_Resume(int* tts_handle);
//typedef int (__cdecl *tts_ResumeFunc)(void*);

// tts_Stop(int* tts_handle);
//typedef int (__cdecl *tts_StopFunc)(void*);

tts_SaveFileFunc _tts_SaveFile;
tts_CallBackFunc _tts_CallBack;
//tts_UserDicFunc  _tts_UserDic;
tts_OpenFunc     _tts_Open;
tts_CloseFunc    _tts_Close;
tts_SpeakFunc    _tts_Speak;
tts_PhonemeFunc  _tts_Phoneme;
//tts_PauseFunc    _tts_Pause;
//tts_ResetFunc    _tts_Reset;
//tts_ResumeFunc   _tts_Resume;
//tts_StopFunc     _tts_Stop;

// test string for test mode
char test[] = {0x1B, 0x5B, '0', 't',
			   0x1B, 0x5B, '1', 't',
			   0x1B, 0x5B, '2', 't',
			   0x1B, 0x5B, '3', 't',
			   0x1B, 0x5B, '4', 't',
			   0x1B, 0x5B, '5', 't',
			   0x1B, 0x5B, '6', 't',
			   0x1B, 0x5B, '7', 't',
			   0x1B, 0x5B, '8', 't',
			   0x1B, 0x5B, '9', 't',
			   '\0'};

// Load the library itself
HINSTANCE cgrmLib = LoadLibrary("TV_ENG32.DLL");
void *tts_handle  = 0;
int err           = 0;
#endif
