// comskip.c
// Copyright (C) 2004 Scott Michael
// Based on the work of Chris Pinkham of MythTV
// comskip is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.
// comskip is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
#include <stdio.h>
#ifdef _WIN32
#include <conio.h>
#include <windows.h>									// needed for sleep command
#include <direct.h>										// needed for getcwd
#include <process.h>
#include <io.h>
#else
#include <errno.h>
#endif
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <memory.h>
#include <time.h>										// needed for play_nice routines
#include <math.h>
#include <inttypes.h>
#include <sys/stat.h>
#include <fcntl.h>
#include "config.h"
#include "libmpeg2\mpeg2.h"
//#include "mpeg2convert.h"
#include "argtable2.h"
#include "comskip.h"

#define bool	int
#define true	1
#define false	0

// Define detection methods
#define BLACK_FRAME		1
#define LOGO			2
#define SCENE_CHANGE	4
#define RESOLUTION_CHANGE		8
#define CC				16
#define AR				32
#define SILENCE			64
#define	CUTSCENE		128


// Define logo detection directions
#define HORIZ	0
#define VERT	1
#define DIAG1	2
#define DIAG2	3

// Define CC types
#define NONE		0
#define ROLLUP		1
#define POPON		2
#define PAINTON		3
#define COMMERCIAL	4

// Define aspect ratio
#define FULLSCREEN		true
#define WIDESCREEN		false


#define AR_TREND	 ((int)(0.8*fps))
#define DEEP_SILENCE	6	//max_volume / DEEP_SILENCE defines deep silence

#define OPEN_INPUT	1
#define OPEN_INI	2
#define SAVE_DMP	3
#define SAVE_INI	4


// max number of frames that can be marked
#define MAX_IDENTIFIERS 300000
#define MAX_COMMERCIALS 50000
int				argument_count;
char **			 argument = NULL;
bool			initialized = false;
const char*		progname = "ComSkip";
FILE*			out_file = NULL;
FILE*			incommercial_file = NULL;
FILE*			ini_file = NULL;
FILE*			plist_cutlist_file = NULL;
FILE*			zoomplayer_cutlist_file = NULL;
FILE*			zoomplayer_chapter_file = NULL;
FILE*			vcf_file = NULL;
FILE*			vdr_file = NULL;
FILE*			projectx_file = NULL;
FILE*			avisynth_file = NULL;
FILE*			videoredo_file = NULL;
FILE*			btv_file = NULL;
FILE*			edl_file = NULL;
FILE*			ipodchap_file = NULL;
FILE*			edlp_file = NULL;
FILE*			bcf_file = NULL;
FILE*			edlx_file = NULL;
FILE*			cuttermaran_file = NULL;
FILE*			chapters_file = NULL;
FILE*			log_file = NULL;
FILE*			womble_file = NULL;
FILE*			mls_file = NULL;
FILE*			mpgtx_file = NULL;
FILE*			dvrcut_file = NULL;
FILE*			dvrmstb_file = NULL;
FILE*			mpeg2schnitt_file = NULL;
FILE*			tuning_file = NULL;
FILE*			training_file = NULL;
FILE*			aspect_file = NULL;
FILE*			cutscene_file = NULL;
extern int		demux_pid;
extern int		selected_audio_pid;
extern int		selected_video_pid;
extern int		demux_asf;

extern int key;
extern char osname[];

#define KDOWN	1
#define KUP		2
#define KLEFT	3
#define KRIGHT	4
#define KNEXT	5
#define KPREV	6
extern int xPos,yPos,lMouseDown;

extern int framenum_infer;

extern struct stat instat;
#define filesize stat.st_size


extern __int64 goppos,packpos,headerpos;
int				vo_init_done = 0;
extern int soft_seeking;

static FILE*	in_file = NULL;

#undef FRAME_WITH_HISTOGRAM
#undef FRAME_WITH_LOGO
#undef FRAME_WITH_AR

typedef struct
{
//	long	frame;
	int		brightness;
	int		schange_percent;
	int		minY;
	int		maxY;
    int		uniform; 
	int		volume;
	double	currentGoodEdge;
	double	ar_ratio;
	bool	logo_present;
	bool	commercial;
	bool	isblack;
	__int64		goppos;
	__int64	pts;
	int		minX;
	int		maxX;
	int		hasBright;
	int		dimCount;
	int    cutscenematch;
	double logo_filter;
	int    xds;
#ifdef FRAME_WITH_HISTOGRAM
	int		histogram[256];
#endif
} frame_info;

frame_info*			frame = NULL;
long				frame_count = 0;
long				max_frame_count;


typedef struct
{
	long	frame;
	int		percentage;
} schange_info;

schange_info*			schange = NULL;
long					schange_count = 0;
long					max_schange_count;

typedef struct
{
	long	frame;
	int		brightness;
	long	uniform;
	int		volume;
	int		cause;
} black_frame_info;

long						black_count = 0;
black_frame_info*			black = NULL;
long						max_black_count;

typedef struct block_info
{
	long			f_start;
	long			f_end;
	unsigned int	b_head;
	unsigned int	b_tail;
	unsigned int	bframe_count;
	unsigned int	schange_count;
	double			schange_rate;						// in changes per second
	double			length;
	double			score;
	int				combined_count;
	int				cc_type;
//	bool			ar;
	double			ar_ratio;
	int				cause;
	int				more;
	int				less;
	int				brightness;
	int				volume;
	int				silence;
	int				uniform;
	int				stdev;
	char			reffer;
	double			logo;
	double			correlation;
	int				strict;
	int				iscommercial;
} block_info;

//block_info*			cblock = NULL;

#define cblock cblock
#define MAX_BLOCKS	1000
struct block_info cblock[MAX_BLOCKS];

long				block_count = 0;
long				max_block_count;

#define		C_c			(1<<1)
#define		C_l			(1<<0)
#define		C_s			(1<<2)
#define		C_a			(1<<5)
#define		C_u			(1<<3)
#define		C_b			(1<<4)
#define		C_r			((long)1<<29)

#define		C_STRICT	(1<<6)
#define		C_NONSTRICT	(1<<7)
#define		C_COMBINED	(1<<8)
#define		C_LOGO		(1<<9)
#define		C_EXCEEDS	(1<<10)
#define		C_AR		(1<<11)
#define		C_SC		(1<<12)
#define		C_H1		(1<<13)
#define		C_H2		(1<<14)
#define		C_H3		(1<<15)
#define		C_H4		((long)1<<16)
#define		C_H5		((long)1<<17)
#define		C_H6		((long)1<<22)
#define		C_v			((long)1<<18)
#define		C_BRIGHT	((long)1<<19)
#define		C_NOTBRIGHT	((long)1<<20)
#define		C_DIM		((long)1<<21)
#define		C_AB		((long)1<<23)
#define		C_AU		((long)1<<24)
#define		C_AL		((long)1<<25)
#define		C_AS		((long)1<<26)
#define		C_AC		((long)1<<26)
#define		C_F			((long)1<<27)
#define		C_t			((long)1<<28)
#define		C_H7		((long)1<<29)


#define C_CUTMASK	(C_c | C_l | C_s | C_a | C_u | C_b | C_t | C_r)
#define CUTCAUSE(c) ( c & C_CUTMASK)


//int minLogo = 30;
//int maxLogo	= 120;

typedef struct
{
	int start;
	int end;
} logo_block_info;

logo_block_info*			logo_block = NULL;
long						logo_block_count = 0;		// How many groups have already been identified. Increment after fill.
long						max_logo_block_count;

extern bool					processCC = false;
extern int					reorderCC;

typedef struct
{
	unsigned char	cc1[2];
	unsigned char	cc2[2];
} ccPacket;

ccPacket		lastcc;
ccPacket		cc;

typedef struct
{
	long	start_frame;
	long	end_frame;
	int		type;
} cc_block_info;

cc_block_info*			cc_block = NULL;
long					cc_block_count = 0;
long					max_cc_block_count;
int						last_cc_type = NONE;
int						current_cc_type = NONE;
bool					cc_on_screen = false;
bool					cc_in_memory = false;

typedef struct
{
	long	frame;
	char	name[40];
	int		v_chip;
	int		duration;
	int		position;
	int		composite1;
	int		composite2;
} XDS_block_info;

XDS_block_info*			XDS_block = NULL;
long					XDS_block_count = 0;
long					max_XDS_block_count;


typedef struct
{
	long			start_frame;
	long			end_frame;
	long			text_len;
	unsigned char	text[256];
} cc_text_info;

cc_text_info*			cc_text = NULL;
long					cc_text_count = 0;
long					max_cc_text_count = 0;


#define AR_UNDEF	0.0
typedef struct
{
	int		start;
	int		end;
//	bool	ar;
	double	ar_ratio;
	int		volume;
	int		height, width;
	int		minX,maxX,minY,maxY;
} ar_block_info;

ar_block_info*			ar_block = NULL;
long					ar_block_count = 0;				// How many groups have already been identified. Increment after fill.
long					max_ar_block_count;
double	last_ar_ratio = 0.0;
double  ar_ratio_trend = 0.0;
int		ar_ratio_trend_counter = 0;
int		ar_ratio_start	= 0;
int		ar_misratio_trend_counter = 0;
int		ar_misratio_start	= 0;


typedef struct
{
	long	start;
	long	end;
} commercial_list_info;

commercial_list_info*	commercial_list = NULL;

int		commercial_count = -1;
struct
{
	long	start_frame;
	long	end_frame;
	int		start_block;
	int		end_block;
	double	length;
} commercial[MAX_COMMERCIALS];


int		reffer_count = -1;
struct
{
	long	start_frame;
	long	end_frame;
} reffer[MAX_COMMERCIALS];



#define MAX_ASPECT_RATIOS	1000
struct
{
	long	frames;
	double	ar_ratio;
} ar_histogram[MAX_ASPECT_RATIOS];
double	dominant_ar;


int						skip_B_frames = 0;
bool					live_tv = false;
bool					output_incommercial = false;
int						incommercial_frames = 1000;
int						live_tv_retries = 4;
int						dvrms_live_tv_retries = 300;
int						standoff = 0;
int						dvrmsstandoff = 120000;
char					incomingCommandLine[MAX_PATH];
char					logofilename[MAX_PATH];
char					logfilename[MAX_PATH];
char					mpegfilename[MAX_PATH];
char					exefilename[MAX_PATH];
char					basename[MAX_PATH];
char					shortbasename[MAX_PATH];
char					inifilename[MAX_PATH];
char					dictfilename[MAX_PATH];
char					out_filename[MAX_PATH];
char					incommercial_filename[MAX_PATH];

char					outputfilename[MAX_PATH];
char					filename[MAX_PATH];
int						curvolume = -1;
extern int						framenum;
unsigned int			frame_period;
//int						audio_framenum = 0;
extern __int64			pts,dts,apts,adts;
extern __int64			initial_pts;
extern int				initial_pts_set;

int			ascr,scr;
int						framenum_real;
int						frames_with_logo;
int						framesprocessed = 0;
char					HomeDir[256];					// comskip home directory
char					tempString[256];
double					average_score;
int						brightness = 0;
long						sum_brightness=0;
long					sum_count;	
int						uniformHistogram[256];
#define UNIFORMSCALE 100
int						brightHistogram[256];
int						blackHistogram[256];
int						volumeHistogram[256];
int						silenceHistogram[256];
int						logoHistogram[256];
int						volumeScale = 10;
int						last_brightness = 0;
int						min_brightness_found;
int						min_volume_found;
int						max_logo_gap;
int						max_nonlogo_block_length;
int						logo_overshoot;
double					logo_quality;
int						width, old_width;
int						height, old_height;
int						ar_width = 0;
int						subsample_video = 0x1f;
//#define MAXWIDTH	800
//#define MAXHEIGHT	600
#define MAXWIDTH	2000
#define MAXHEIGHT	1200

int haslogo[MAXWIDTH*MAXHEIGHT];

// unsigned char		oldframe[MAXWIDTH*MAXHEIGHT];

// variables defining options with defaults
int					verbose = 0;						// show extra info
double				fps = 25.0;						// frames per second (NTSC=29.970, PAL=25)
int					border = 10;						// border around edge of video to ignore
int					ticker_tape=0;						// border from bottom to ignore
int					ignore_side=0;
int					max_brightness = 60;				// frame not black if any pixels checked are greater than this (scale 0 to 255)
int					maxbright = 1;
int					min_hasBright = 255000;
int					min_dimCount = 255000;
int					test_brightness = 40;				// frame not pure black if any pixels are greater than this, will check average
int					max_avg_brightness = 19;			// maximum average brightness for a dim frame to be considered black (scale 0 to
int					max_volume = 500;	
int					max_silence = 100;
int					min_silence = 0;
int					punish_no_logo = true;
int					validate_silence = true;
int					validate_uniform = true;
int					validate_scenechange = true;
int					validate_ar = true;

int					punish = 0;
int					reward = 0;
int					min_volume=0;
int					min_uniform = 0;
int					volume_slip = 40;
extern int ms_audio_delay;
int					max_repair_size = 40;
int					variable_bitrate = 1;

extern int is_h264;
														///brightness (scale 0 to 255)
char				ini_text[40000];
														///255)
double				max_commercialbreak = 600;	// maximum length in seconds to consider a segment a commercial break
double				min_commercialbreak = 20;	// minimum length in seconds to consider a segment a commercial break
double				max_commercial_size = 120;	// maximum time in seconds for a single commercial
double				min_commercial_size = 4;	// mimimum time in seconds for a single commercial
double				min_show_segment_length = 120.0;
bool				require_div5 = 0;					// set=1 to only mark breaks divisible by 5 as a commercial.
double				div5_tolerance = -1;
bool				play_nice = false;
double				global_threshold = 1.05;
bool				intelligent_brightness = false;
double				logo_threshold = 0.80;
double				logo_max_percentage_of_screen = 0.12;
int					logo_filter = 0;
int					non_uniformity = 500;
int					brightness_jump = 200;
double				black_percentile = 0.0076;
double				uniform_percentile = 0.003;
double				score_percentile = 0.71;
double				logo_percentile = 0.92;
double				logo_fraction = 0.40;
int					commDetectMethod = BLACK_FRAME + LOGO + RESOLUTION_CHANGE +  AR + SILENCE; // + CC
int					giveUpOnLogoSearch = 2000;			// If no logo is identified after x seconds into the show - give up.
int					delay_logo_search = 0;			// If no logo is identified after x seconds into the show - give up.
bool				cut_on_ar_change = 1;
int					added_recording = 14;
int					after_start = 0;
int					before_end = 0;
int					delete_show_after_last_commercial = false;
int					delete_show_before_first_commercial = false;
int					delete_block_after_commercial = 0;
int					min_commercial_break_at_start_or_end = 39;
int					always_keep_first_seconds = 0;
int					always_keep_last_seconds = 0;

bool				connect_blocks_with_logo = true;
int					delete_show_before_or_after_current = false;
bool				deleteLogoFile = true;
bool				useExistingLogoFile = true;
bool				startOverAfterLogoInfoAvail = true;
int					doublCheckLogoCount = 0;
bool				output_default = true;
bool				output_chapters = false;
bool				sage_framenumber_bug = false;
bool				sage_minute_bug = false;
bool				enable_mencoder_pts = false;
bool				output_plist_cutlist = false;
bool				output_zoomplayer_cutlist = false;
bool				output_zoomplayer_chapter = false;
bool				output_videoredo = false;
bool				output_ipodchap = false;
int					videoredo_offset = 2;
int					edl_offset = 0;
bool				output_edl = false;
bool				output_edlp = false;
bool				output_bsplayer = false;
bool				output_edlx = false;
bool				output_btv = false;
bool				output_cuttermaran = false;
bool				output_mpeg2schnitt = false;
char				cuttermaran_options[1024];
char				mpeg2schnitt_options[1024];
char				avisynth_options[1024];
char				dvrcut_options[1024];
bool				output_demux = false;
bool				output_data = false;
bool				output_srt = false;
bool				output_smi = false;
bool				output_timing = false;
bool				output_womble = false;
bool				output_mls = false;
bool				output_mpgtx = false;
bool				output_dvrcut = false;
bool				output_dvrmstb = false;
bool				output_vdr = false;
bool				output_vcf = false;
bool				output_projectx = false;
bool				output_avisynth = false;
bool				output_debugwindow = false;
bool				output_console = true;
int					disable_heuristics = 0;
char                windowtitle[1024] = "Comskip - %s";
bool				output_tuning = false;
bool				output_training = false;
bool				output_false = false;
bool				output_aspect = false;
int					noise_level=5;
bool				framearray = true;
bool				output_framearray = false;
bool				only_strict = false;
double				length_strict_modifier = 3.0;
double				length_nonstrict_modifier = 1.5;
double				combined_length_strict_modifier = 2.0;
double				combined_length_nonstrict_modifier = 1.25;
double				logo_present_modifier = 0.01;
double				punish_modifier = 2.0;
double				punish_threshold = 1.3;
double				reward_modifier = 0.5;
int					after_logo=0;
int					before_logo=0;
int					shrink_logo=5;
int					where_logo=0;
double				excessive_length_modifier = 0.01;
double				dark_block_modifier = 0.3;
int					padding = 0;
int					remove_before = 0;
int					remove_after = 0;
double				min_schange_modifier = 0.5;
double				max_schange_modifier = 2.0;
int					schange_threshold = 90;
int					schange_cutlevel = 15;
double				cc_commercial_type_modifier = 4.0;
double				cc_wrong_type_modifier = 2.0;
double				cc_correct_type_modifier = 0.75;
double				ar_wrong_modifier = 2.0;
double				ar_rounding = 100;
double				ar_delta = 0.08;
long				avg_brightness = 0;
long				maxi_volume = 0;
long				avg_volume = 0;
long				avg_silence = 0;
long				avg_uniform = 0;
double				avg_schange = 0.0;
double				dictionary_modifier = 1.05;
bool				aggressive_logo_rejection = false;
unsigned int		min_black_frames_for_break = 1;
bool				detectBlackFrames;
bool				detectSceneChanges;
unsigned char*		frame_ptr;
// bool				frameIsBlack;
bool				sceneHasChanged;
int					sceneChangePercent;
bool				lastFrameWasBlack = false;
bool				lastFrameWasSceneChange = false;
int					histogram[256];
int					lastHistogram[256];

#define				MAXCSLENGTH		400*300
#define				MAXCUTSCENES	8


void LoadCutScene(const char *filename);
void RecordCutScene(int frame_count,int brightness);

char cutscenefile[1024];
char cutscenefile1[1024];
char cutscenefile2[1024];
char cutscenefile3[1024];
char cutscenefile4[1024];
char cutscenefile5[1024];
char cutscenefile6[1024];
char cutscenefile7[1024];
char cutscenefile8[1024];

int					cutscenematch;
int					cutscenedelta = 10;
int					cutsceneno = 0;


int					cutscenes=0;
unsigned char		cutscene[MAXCUTSCENES][MAXCSLENGTH];
int					csbrightness[MAXCUTSCENES];
int					cslength[MAXCUTSCENES];

// int					cssum[MAXCUTSCENES];
// int					csmatch[MAXCUTSCENES];


char				debugText[20000];
bool				logoInfoAvailable;
bool				secondLogoSearch = false;
bool				logoBuffersFull = false;
int					logoTrendCounter = 0;
double				logoFreq = 1;						// times fps between logo checks
int					num_logo_buffers = 50;				// How many frames to compare at a time for logo detection;
bool				lastLogoTest = false;
// int					logoTrendStartFrame;
int*				logoFrameNum = NULL;				// Keep track of the frame numbers of each buffer
int					oldestLogoBuffer;					// Which buffer is the oldest?
// int					lastRealLogoChange;
bool				curLogoTest = false;
int					minHitsForTrend = 10;
// bool				hindsightLogoState = true;
double				logoPercentage = 0.0;
bool				reverseLogoLogic = false;
#define MULTI_EDGE_BUFFER 0
#if MULTI_EDGE_BUFFER
unsigned char **	horiz_edges = NULL;				// rotating storage for detected horizontal edges
unsigned char **	vert_edges = NULL;					// rotating storage for detected vertical edges
#else
unsigned char horiz_count[MAXHEIGHT*MAXWIDTH];
unsigned char vert_count[MAXHEIGHT*MAXWIDTH];
#endif
double				borderIgnore = .05;					// Percentage of each side to ignore for logo detection
int					subtitles = 0;
int					logo_at_bottom = 0;
int					edge_radius = 2;
int					int_edge_radius = 2;
int					edge_step = 1;
int					edge_level_threshold = 5;
int					edge_weight = 10;
int					edge_count = 0;
int					hedge_count = 0;
int					vedge_count = 0;
int					newestLogoBuffer = -1;				// Which buffer is the newest? Increments prior to fill so start at -1
unsigned char **	logoFrameBuffer = NULL;			// rotating storage for frames
int					logoFrameBufferSize = 0;
int					lwidth;
int					lheight;
int					logoMinX;
int					logoMaxX;
int					logoMinY;
int					logoMaxY;
int					play_nice_start = -1;
int					play_nice_end = -1;
long				play_nice_sleep = 10L;
FILE *dump_data_file = (FILE *)NULL;
uint8_t				ccData[500];
int					ccDataLen;
static uint8_t	    prevccData[500];
static int			prevccDataLen;
long				cc_count[5] = { 0, 0, 0, 0, 0 };
int					most_cc_type = NONE;
unsigned char **	cc_screen = NULL;
unsigned char **	cc_memory = NULL;
int					minY;								// The top of the picture for aspect ratio calculation
int					maxY;								// The bottom of the picture for aspect ratio calculation
int					minX;								// The top of the picture for aspect ratio calculation
int					maxX;								// The bottom of the picture for aspect ratio calculation
//bool				currentAR;
//bool				lastAR;
//bool				showAvgAR;
bool				isSecondPass = false;
long				lastFrame = 0;
long				lastFrameCommCalculated = 0;
bool				ccCheck = false;
bool				loadingCSV = false;
bool				loadingTXT = false;
int					helpflag = 0;
int					recalculate=0;
char *helptext[]=
{

	"Help: press any key to remove",
	"Key          Action",
	"Arrows	        Reposition current location",
	"PgUp/PgDn      Reposition current location",
	"n/p            Jump to next/previous cutpoint",
	"e/b            Jump to next/previous end of cblock",
	"z/u            Zoom in/out on the timeline",
	"g              Graph on/off",
	"x              XDS info on/off",
	"t              Toggle current cblock between show and commercial",
	"w              Write the new cutpoints to the ouput files",
	"c              Dump this frame as CutScene"
	"F2             Reduce the max_volume detection level",
	"F3             Reduce the non_uniformity detection level",
	"F4             Reduce the max_avg_brighness detection level",
	"",
	"During commercial break review",
	"e              Set end of commercial to this position",
	"b              Set begin of commercial to this position",
	"i              Insert a new commercial",
	"d              Delete the commercial at current location",
	"s              Jump to Start of the recording",
	"f              Jump to Finish of the recording",
	0
};

double	currentGoodEdge = 0.0;


int lineStart[MAXHEIGHT];		/* Area to include for black frame detection, non logo area */
int lineEnd[MAXHEIGHT];

unsigned char hor_edgecount[MAXHEIGHT*MAXWIDTH];
unsigned char ver_edgecount[MAXHEIGHT*MAXWIDTH];
unsigned char max_br[MAXHEIGHT*MAXWIDTH];
unsigned char min_br[MAXHEIGHT*MAXWIDTH];
unsigned char horiz_edgemask[MAXHEIGHT*MAXWIDTH];
unsigned char vert_edgemask[MAXHEIGHT*MAXWIDTH];


#define RGB
#ifdef RGB
unsigned char graph[MAXHEIGHT*MAXWIDTH*3];
#else
unsigned char graph[MAXHEIGHT*MAXWIDTH];
#endif

int gy=0;

// Function Prototypes
bool				BuildBlocks(bool recalc);
void				Recalc(void);
double				ValidateBlackFrames(long reason, double ratio, int remove);
int					DetectCommercials(int);
bool				BuildMasterCommList(void);
void				WeighBlocks(void);
bool				OutputBlocks(void);
void        OutputAspect(void);
void        OutputTraining(void);
bool ProcessLogoTest(int framenum_real, int curLogoTest, int close);
void        OutputStrict(double len, double delta, double tol);
int					InputReffer(char *ext);
bool				IsStandardCommercialLength(double length, double tolerance, bool strict);
bool				LengthWithinTolerance(double test_length, double expected_length, double tolerance);
double				FindNumber(char* str1, char* str2, double v);
char *				FindString(char* str1, char* str2, char *v);
void				AddIniString( char *s);
char*				intSecondsToStrMinutes(int seconds);
char*				dblSecondsToStrMinutes(double seconds);
FILE*				LoadSettings(int argc, char ** argv);
int					GetAvgBrightness(void);
bool				CheckFrameIsBlack(void);
void				BuildBlackFrameCommList(void);
bool				CheckSceneHasChanged(void);
#if 0
void				BuildSceneChangeCommList(void);
void				BuildSceneChangeCommList2(void);
#endif
void				PrintLogoFrameGroups(void);
void				PrintCCBlocks(void);
void				ResetLogoBuffers(void);
void				EdgeDetect(unsigned char* frame_ptr, int maskNumber);
void				EdgeCount(unsigned char* frame_ptr);
void				FillLogoBuffer(void);
bool				SearchForLogoEdges(void);
double				CheckStationLogoEdge(unsigned char* testFrame);
void				SetEdgeMaskArea(unsigned char* temp);
int					ClearEdgeMaskArea(unsigned char* temp, unsigned char* test);
int					CountEdgePixels(void);
void				DumpEdgeMask(unsigned char* buffer, int direction);
void				DumpEdgeMasks(void);
void				BuildBlackFrameAndLogoCommList(void);
bool				CheckFramesForLogo(int start, int end);
char				CheckFramesForCommercial(int start, int end);
char				CheckFramesForReffer(int start, int end);
void				SaveLogoMaskData(void);
void				LoadLogoMaskData(void);
double				CalculateLogoFraction(int start, int end);
bool				CheckFrameForLogo(int i);
int					CountSceneChanges(int StartFrame, int EndFrame);
void				Debug(int level, char* fmt, ...);
void				InitProcessLogoTest(void);
void				InitComSkip(void);
void				InitLogoBuffers(void);
void				FindIniFile(void);
double				FindScoreThreshold(double percentile);
void				OutputLogoHistogram(int buckets);
void				OutputbrightHistogram(void);
void				OutputuniformHistogram(void);
void				OutputHistogram(int *histogram, int scale, char *title, bool truncate);
int					FindBlackThreshold(double percentile);
int					FindUniformThreshold(double percentile);
void				OutputFrameArray(bool screenOnly);
void				OutputFrame();
void				OpenOutputFiles();
void				InitializeFrameArray(long i);
void				InitializeBlackArray(long i);
void				InitializeSchangeArray(long i);
void				InitializeLogoBlockArray(long i);
void				InitializeARBlockArray(long i);
void				InitializeBlockArray(long i);
void				InitializeCCBlockArray(long i);
void				InitializeCCTextArray(long i);
void				PrintArgs(void);
void        close_dump(void);
void				OutputCommercialBlock(int i, long prev, long start, long end, bool last);
void				ProcessCSV(FILE *);
void				OutputCCBlock(long i);
void				ProcessCCData(void);
bool				CheckOddParity(unsigned char ch);
void				AddNewCCBlock(long current_frame, int type, bool cc_on_screen, bool cc_in_memory);
char*				CCTypeToStr(int type);
int					DetermineCCTypeForBlock(long start, long end);
double				AverageARForBlock(int start, int end);
void				SetARofBlocks(void);
bool				ProcessCCDict(void);
int					FindBlock(long frame);
void				BuildCommListAsYouGo(void);
void				BuildCommercial(void);
int					RetreiveVolume (int f);
void InsertBlackFrame(int f, int b, int u, int v, int c);
extern void DecodeOnePicture(FILE * f, int fp);



int CEW_init(int argc, char *argv[]);

char *CauseString(int i)
{
	static char cs[4][80];
	static int ii=0;
	char *c = &(cs[ii][0]);
	char *rc = &(cs[ii][0]);

	*c++ = (i & C_H7		? '7' : ' ');
	*c++ = (i & C_H6		? '6' : ' ');
	*c++ = (i & C_H5		? '5' : ' ');
	*c++ = (i & C_H4		? '4' : ' ');
	*c++ = (i & C_H3		? '3' : ' ');
	*c++ = (i & C_H2		? '2' : ' ');
	*c++ = (i & C_H1		? '1' : ' ');

    if (strncmp((char*)cs[ii],"       ",7))
		*c++ = '{';
	else
		*c++ = ' ';
	*c++ = (i & C_SC		? 'F' : ' ');
	*c++ = (i & C_AR		? 'A' : ' ');
	*c++ = (i & C_EXCEEDS	? 'E' : ' ');
	*c++ = (i & C_LOGO		? 'L' : (i & C_BRIGHT			? 'B': ' '));
	*c++ = (i & C_COMBINED ? 'C' : ' ');
	*c++ = (i & C_NONSTRICT? 'N' : ' ');
	*c++ = (i & C_STRICT	? 'S' : ' ');
	*c++ = (i & 2			? 'c' : (i & C_t			? 't': ' '));
	*c++ = (i & 1			? 'l' : (i & C_v			? 'v': ' '));
	*c++ = (i & 4			? 's' : ' ');
	*c++ = (i & 32			? 'a' : ' ');
	*c++ = (i & 8			? 'u' : ' ');
	*c++ = (i & 16			? 'b' : ' ');
	*c++ = (i & C_r			? 'r' : ' ');
	*c++ = 0;

	ii = (ii + 1) % 4;

	return(rc);
}

double ValidateBlackFrames(long reason, double ratio, int remove) {
	int i,k,j,last, last_length, prev;
	int prev_cause;
	int strict_count = 0;
	int negative_count = 0;
	int positive_count = 0;
	int count = 0;
	int total_cause;
	double length,summed_length;
	int incommercial;
	char *r = " -undefined- ";
	if (reason == C_b)
		r = "Black Frame  ";
	if (reason == C_v)
		r = "Volume       ";
	if (reason == C_s)
		r = "Scene Change ";
	if (reason == C_c)
		r = "Change       ";
	if (reason == C_u)
		r = "Uniform Frame";
	if (reason == C_a)
		r = "Aspect Ratio ";
	if (reason == C_t)
		r = "Cut Scene    ";
	if (reason == C_l)
		r = "Logo         ";

	if (ratio == 0.0)
		return(0.0);
#ifndef undef
	incommercial = 0;
	i = 0; // search for reason
	strict_count = 0;
	count = 0;
	last = 0;
	length = 0.0;
	summed_length = 0.0;
	while(i < black_count) {
		prev = i;
		while (i < black_count && (black[i].cause & reason) == 0)
		{
			i++;
		}
		k = i;
		while (k < black_count && (black[k+1].cause & reason) != 0 && black[k+1].frame == black[k].frame+1) {
			k++;
		}
		if (i < black_count) {
			length = (black[(i+k)/2].frame - black[last].frame) / fps;
			if (length > max_commercial_size)
			{
				if (incommercial)
				{
					incommercial = 0;
					if (summed_length < min_commercialbreak && summed_length > 4.7 && black[(i+k)/2].frame < frame_count * 6 / 7  && black[last].frame > frame_count * 1 / 7 )
					{
						negative_count++;
						Debug (10,"Negative %s cutpoint at %6i, commercial too short\n", r,black[last].frame);
					} else
						positive_count++;
					summed_length = 0.0;
				} else {
					positive_count++;
				}
				summed_length = 0.0;
			} else {
				summed_length += length;
				if (incommercial && summed_length > max_commercialbreak )
				{
					if (black[(i+k)/2].frame < frame_count * 6 / 7)
					{
						negative_count++;
						Debug (10,"Negative %s cutpoint at %6i, commercial too long\n", r,black[(i+k)/2].frame);
					}
				} else {
					positive_count++;
					incommercial = 1;
				}
			}
		}
		last = (i+k)/2;
		i = k+1;
	}
	Debug (1,"Distribution of %s cutting: %3i positive and %3i negative, ratio is %6.4f\n", r,	positive_count, negative_count, (negative_count > 0 ? (double)positive_count / (double)negative_count : 9.99));

	if ((logoPercentage < logo_fraction || logoPercentage > logo_percentile) && negative_count > 1)
	{

		Debug (1,"Confidence of %s cutting: %3i negative without good logo is too much\n", r,	negative_count);
		if (remove) {
			for (k = black_count - 1; k >= 0; k--) {
				if (black[k].cause & reason) {
					black[k].cause &= ~reason;
					if (black[k].cause == 0) {
						for (j = k; j < black_count - 1; j++) {
							black[j] = black[j + 1];
						}
						black_count--;
					}
				}
			}
		}

 /*
		
		if (negative_count > 1 && reason == C_v)
		{
			Debug(1, "Too mutch Silence Frames, disabling silence detection\n");
			commDetectMethod &= ~SILENCE;
		}
		if (negative_count > 1 && reason == C_s)
		{
			Debug(1, "Too mutch Scene Change, disabling Scene Change detection\n");
			commDetectMethod &= ~SCENE_CHANGE;
		}
*/
	}


#endif	
	
	i = 1;
	strict_count = 0;
	count = 0;
	prev_cause = 0;
	while(i < black_count) {
		total_cause = black[i].cause;
		k = i;
		while (k < black_count && black[k+1].frame == black[k].frame+1) {
			k++;
			total_cause |= black[k].cause;
		}
		last_length = (k - i)/2;
		last = (i+k)/2;
		if ((total_cause & reason) && (prev_cause & reason)) {
			j = i-1;
			while (j > 0 && black[j-1].frame == black[j].frame - 1)
				j--;

			length = (black[i].frame - black[(i-1+j)/2].frame) / fps;
			if (length > 1.0 && length * fps < max_commercial_size * fps) {
				count++;
				if (IsStandardCommercialLength(length, (i-j)/fps + 0.8, false)) {
//				if (length > max_commercial_size) {
					strict_count++;
				}
			}
		}
		prev_cause = reason;
		k++;
		i = k;
	}

	if (strict_count < 2 || 100*strict_count < 100*count / ratio) {
		Debug (1,"Confidence of %s cutting: %3i out of %3i are strict, too low\n", r,	strict_count, count);
		if (remove) {
			for (k = black_count - 1; k >= 0; k--) {
				if (black[k].cause & reason) {
					black[k].cause &= ~reason;
					if (black[k].cause == 0) {
						for (j = k; j < black_count - 1; j++) {
							black[j] = black[j + 1];
						}
						black_count--;
					}
				}
			}
		}
	} else
	Debug (1,"Confidence of %s cutting: %3i out of %3i are strict\n", r,	strict_count, count);
	return (count > 0 ? (double)strict_count / (double) count : 0);
}

//Function code blocks

bool BuildBlocks(bool recalc) {
	int i = 0;
	int j = 0;
	int k = 0;
	int a = 0;
	int count;
	int v_count;
	int b_count;
	int black_start;
	int black_end;
	int cause = 0;
	int black_threshold;
	int uniform_threshold;
	int prev_start = 1;
	int prev_head = 0;
	
	long b_start, b_end, b_counted;
//	char *t = "";
	
//	max_block_count = 80;
	max_block_count = MAX_BLOCKS;
	block_count = 0;
//	cblock = malloc(max_block_count * sizeof(block_info));
	
	recalculate = recalc;
	InitializeBlockArray(0);
	
	// If there are no black frames, nothing can be done
//	if (!black_count && !ar_block_count) return (false);
	
//	OutputHistogram(volumeHistogram, volumeScale, "Volume", true);

	if (!recalc) {
		// Eliminate frames that are too bright from black frame list
		if (intelligent_brightness) {
			OutputbrightHistogram();
			max_avg_brightness = black_threshold = FindBlackThreshold(black_percentile);
			Debug(1, "Setting brightness threshold to %i\n", black_threshold);
		}
		if ((intelligent_brightness && non_uniformity > 0) 
//			|| 	(commDetectMethod & BLACK_FRAME && non_uniformity == 0) // Diabled
			) {
			OutputuniformHistogram ();
			non_uniformity = uniform_threshold = FindUniformThreshold(uniform_percentile);
			Debug(1, "Setting uniform threshold to %i\n", uniform_threshold);
			
			if (commDetectMethod & BLACK_FRAME) {
				for (i = 1; i < frame_count; i++) {
					frame[i].isblack &= ~C_u;
					if (/*!(frame[i].isblack & C_b) && */ non_uniformity > 0 && frame[i].uniform < non_uniformity && frame[i].brightness < 180/*&& frame[i].volume < max_volume*/ )
						InsertBlackFrame(i,frame[i].brightness,frame[i].uniform,frame[i].volume, (int)C_u);
				}
			}
		}		
	}

	if (non_uniformity < min_uniform + 100)
		non_uniformity = min_uniform + 100;

	if (framearray) {						// Find minumum volume around black frame
		for (k = black_count - 1; k >= 0; k--) {
			if (black[k].cause == C_s || black[k].cause == C_c || black[k].cause == (C_c|C_s) ) {
				i = black[k].frame-volume_slip;		// Find quality of silence around black frame
				if (i < 0) i = 0;
				j = black[k].frame+volume_slip;
				if (j>frame_count) j = frame_count;
				count = 0;
				for (a=i; a<j; a++) {
					if (frame[a].volume < max_volume/4) 
						count += volume_slip;
					else 
					if (frame[a].volume < max_volume) 
						count++;

				}
				if (count > volume_slip/4) {
					black[k].volume = max_volume /2;
				} else {
					black[k].volume = max_volume *10 ;
				}
			} else {
				i = black[k].frame-volume_slip;		// Find minimum volume around black frame
				if (i < 0) i = 0;
				j = black[k].frame+volume_slip;
				if (j>frame_count) j = frame_count;
				for (a=i; a<j; a++)
					if (frame[a].volume >= 0) 
						if (black[k].volume > frame[a].volume)
							black[k].volume = frame[a].volume;
			}
		}
		for (k = ar_block_count - 1; k >= 0; k--) {
			i = ar_block[k].end-volume_slip;
			if (i < 0) i = 0;
			j = ar_block[k].end+volume_slip;
			if (j>frame_count) j = frame_count;
			ar_block[k].volume = frame[i].volume;
			for (a=i; a<j; a++)
				if (frame[a].volume >= 0) 
					if (ar_block[k].volume > frame[a].volume) {
						ar_block[k].volume = frame[a].volume;
						ar_block[k].end = a;
						if (k < ar_block_count - 1)
							ar_block[k+1].start = a;
					}
		}

	}
	for (k = 1; k < 255; k++) {
		if (volumeHistogram[k] > 10) {
			min_volume = (k-1)*volumeScale;
			break;
		}
	}

	for (k = 1; k < 255; k++) {
		if (uniformHistogram[k] > 10) {
			min_uniform = (k-1)*UNIFORMSCALE;
			break;
		}
	}

	for (k = 0; k < 255; k++) {
		if (brightHistogram[k] > 1) {
			min_brightness_found = k;
			break;
		}
	}
/*
	if (framearray) {
		logo_quality= 1.0;
		for (k = 0; k < logo_block_count; k++) {
			if ((logo_block[k].end - logo_block[k].start)/fps > 20) {
				for (i=logo_block[k].start+5*fps; i < logo_block[k].end-5*fps; i++) {
					if (frame[i].logo_present &&
						logo_quality > frame[i].currentGoodEdge)
						logo_quality = frame[i].currentGoodEdge;
				}
			}	
		}
	}

	if (framearray) {
		j = 10;
		k = 0;
		for (i = 1; i < frame_count; i += (int) fps) {
			logoHistogram[(int)(frame[i].currentGoodEdge * (j - 1))]++;
			k++;
		}

		i = 0;
		k = logoHistogram[0];
		for (i = 0; i < j; i++) {
			if (k > logoHistogram[i]) {
				k = logoHistogram[i];
				logo_quality = (double) i / (double) j;
			}
		}
	}
*/
	
	if (max_volume > 0) {
		for (k = black_count - 1; k >= 0; k--) {
			if ((black[k].cause & C_t) != 0)
				continue;
			if (black[k].volume >  max_volume 
//				&& 
//				( black[k].frame > 10 && (int)frame[black[k].frame-2].brightness < (int)frame[black[k].frame].brightness + 50 &&
//				 black[k].frame < frame_count - 10 && (int)frame[black[k].frame+2].brightness < (int) frame[black[k].frame].brightness + 50 )
//			   || black[k].volume >  max_volume * 1.5 
			   )
			{

				Debug
					(	
					12,
					"%i - Removing black frame %i, from black frame list because volume %i is more than %i, brightness %i, uniform %i\n",
					k,
					black[k].frame,
					black[k].volume,
					max_volume,
					black[k].brightness,
					black[k].uniform
					);

				for (j = k; j < black_count - 1; j++) {
					black[j] = black[j + 1];
				}
				
				black_count--;
			}
		}
	}

		for (k = black_count - 1; k >= 0; k--) {
			if ((black[k].cause & C_t) != 0)
				continue;

			if ((black[k].cause & C_r) != 0)
				continue;

			if ((black[k].cause & C_b) && black[k].brightness > max_avg_brightness) {
/*
			Debug
			(	
			6,
			"%i - Removing black frame %i, from black frame list because %i is more than %i, uniform %i\n",
			k,
			black[k].frame,
			black[k].brightness,
			max_avg_brightness,
			black[k].uniform
			);
*/
				for (j = k; j < black_count - 1; j++) {
					black[j] = black[j + 1];
				}
				
				black_count--;
			}
		}
		if (non_uniformity > 0) {
			for (k = black_count - 1; k >= 0; k--) {
				if ((black[k].cause & C_t) != 0)
					continue;
				if ((black[k].cause & C_u) && black[k].uniform > non_uniformity) {
				/*				Debug
				(	
				6,
				"%i - Removing uniform frame %i, from black frame list because %i is more than %i, brightness %i\n",
				k,
				black[k].frame,
				black[k].uniform,
				non_uniformity,
				black[k].brightness
				);
					*/
					for (j = k; j < black_count - 1; j++) {
						black[j] = black[j + 1];
					}
					
					black_count--;
				}
			}
		}

	if (cut_on_ar_change==2) {
		if (logoPercentage > logo_fraction && logoPercentage < logo_percentile)
			cut_on_ar_change = 1;
//		else
//			ar_wrong_modifier=1;
	}
	
	if (cut_on_ar_change==1) {
//		if (logoPercentage < logo_fraction || logoPercentage > logo_percentile)
//			cut_on_ar_change = 2;
	}

	if (((commDetectMethod & LOGO) && cut_on_ar_change ) || cut_on_ar_change >= 2) {
//	if (cut_on_ar_change ) {
		for (i = 0; i < ar_block_count; i++) {
			if ((cut_on_ar_change == 1 || ar_block[i].volume < max_volume) && 
				ar_block[i].ar_ratio != AR_UNDEF && ar_block[i+1].ar_ratio != AR_UNDEF) {
					a = ar_block[i].end;
//					if (a > 20 * fps)
						InsertBlackFrame(a,frame[a].brightness,frame[a].uniform,frame[a].volume, C_a);
			}
		}
	}

	if (ValidateBlackFrames(C_b, 3.0, false) < 1 / 3.0)
		Debug(8, "Black Frame cutting too low\n");

	if (validate_scenechange /* || (logoPercentage < logo_fraction || logoPercentage > logo_percentile) */)
		ValidateBlackFrames(C_s, ((logoPercentage < logo_fraction || logoPercentage > logo_percentile) ? 1.2 : 3.5), true);

	//	ValidateBlackFrames(C_c, 3.0, true);

	if (validate_uniform)
		ValidateBlackFrames(C_u, 3.0, true);


	if (commDetectMethod & SILENCE) {
		k = 0;
		for (i = 0; i < frame_count; i++) {
			if (frame[i].volume < max_volume) k++;
		}
/*
		if (k * 100 / frame_count > 25) {
			Debug(8, "Too mutch Silence Frames (%d%%), disabling silence detection\n", k * 100 / frame_count);

			ValidateBlackFrames(C_v, 1.0, true);
			commDetectMethod &= ~SILENCE;
			validate_silence = 0;
		} else 
*/		if (validate_silence)
			ValidateBlackFrames(C_v, 3.0, true);
	}

//		if (logoPercentage < logo_fraction)
//	if (cut_on_ar_change == 2)
//		ValidateBlackFrames(C_a, 3.0, true);


	Debug(8, "Black Frame List\n---------------------------\nBlack Frame Count = %i\nnr \tframe\tbright\tuniform\tvolume\t\tcause\tdimcount  bright\n", black_count);
	for (k = 0; k < black_count; k++) {
		if (black[k].frame == 3200)
			k = k;
		Debug(8, "%3i\t%6i\t%6i\t%6i\t%6i\t%6s\t%6i\t%6i\n", k, black[k].frame, black[k].brightness, black[k].uniform, black[k].volume,&(CauseString(black[k].cause)[10]), frame[black[k].frame].dimCount, frame[black[k].frame].hasBright);
		if (k+1 < black_count && black[k].frame+1 != black[k+1].frame)
			Debug(8, "-----------------------------\n");

	}
	
	
	// add black frame at end to enable usage of last cblock
	InsertBlackFrame(framesprocessed,0,0,0,C_b);
/*
	InitializeBlackArray(black_count);
	black[black_count].frame = framesprocessed;
	black[black_count].brightness = 0;
	black[black_count].uniform = 0;
	black[black_count].volume = 0;
	black[black_count].cause = 0;
	black_count++;
*/
	//Create blocks


	
	i = 0;
	j = 0;
//	if (((commDetectMethod & LOGO) && cut_on_ar_change ) || cut_on_ar_change == 2)
//		a = 0;
//	else
		a = ar_block_count;			// Don't cut on AR when logo disabled
	cause = 0;
	block_count = 0;
	prev_start = 1;
	prev_head = 0;
	
again:
	while(i < black_count || a < ar_block_count) {
		if (!(commDetectMethod & LOGO) && i < black_count && (black[i].cause & (C_s | C_l))) {
//			i++; // Skip logo cuts and brighness cuts when not enough logo detected
//			goto again;
			i = i;
		}
		cause = 0;
		b_start = black[i].frame;
		cause |= black[i].cause;
		b_end = b_start;
		j = i + 1;

		v_count = 0;
		b_count = 0;
		black_start = 0;
		black_end = 0;
		//Find end of next black cblock
		while(j < black_count && (black[j].frame - b_end < (int)fps)) { //Allow for 2 missing black frames
			if (black[j].frame - b_end > 2 && 
          (((black[j].cause & (C_v)) != 0 &&  (cause & (C_v)) == 0) ||
           ((black[j].cause & (C_v)) == 0 &&  (cause & (C_v)) != 0)))
			{

				Debug
					(	
					6,
					"At frame %i there is a gap of %i frames in the blackframe list\n",
					black[j].frame,
					black[j].frame - b_end
					);
			}

			if ((black[j].cause & (C_b | C_s | C_u | C_r)) != 0) {				
				b_count++;
				if (black_start == 0)
					black_start = black[j].frame;
				black_end = black[j].frame;

			}
			if ((black[j].cause & (C_v)) != 0) 
				v_count++;
			if (black[j].cause == C_a) {
				cause |= black[j].cause;
				j++;
			}
			else if (cause == C_a) {
				cause |= black[j].cause;
				b_start = b_end = black[j++].frame;
			} else {
				cause |= black[j].cause;
				b_end = black[j++].frame;
			}
		}
		i = j;
		
		if (b_count > 0 && v_count > 3*b_count) {
			b_start = black_start;
			b_end = black_end;
			b_counted = b_count;
		}
		cblock[block_count].cause = cause;
		
		//Do it this way for in roundoff problems
		b_counted = (b_end - b_start + 1)/2;

		cblock[block_count].b_head = prev_head;
			cblock[block_count].f_start = prev_start - cblock[block_count].b_head;
		if (b_end == framesprocessed)
			cblock[block_count].f_end = framesprocessed;
		else
			cblock[block_count].f_end = b_start + b_counted - 1;
		cblock[block_count].b_tail = b_counted;		//half on the tail of this cblock
		cblock[block_count].bframe_count = cblock[block_count].b_head + cblock[block_count].b_tail;
		cblock[block_count].length = (cblock[block_count].f_end - cblock[block_count].f_start) / fps;

		//If first cblock is < 1 sec. throw it away
		if( block_count > 0 
			|| cblock[block_count].f_end - cblock[block_count].f_start > fps			
			) {
/*
			Debug(6, "Creating cblock %i From %i (%i) to %i (%i) because of %s with %i head and %i tail\n",
					block_count, cblock[block_count].f_start, (cblock[block_count].f_start + cblock[block_count].b_head),
					cblock[block_count].f_end, (cblock[block_count].f_end - cblock[block_count].b_tail),
					t,
					cblock[block_count].b_head, cblock[block_count].b_tail);
*/		
			block_count++;
			InitializeBlockArray(block_count);
			prev_start = b_end + 1;							//cblock starts at end of black initially
			prev_head = b_end - b_start - b_counted + 1;	//remaining black from previous cblock tail
		}
	}


#if 0
	//Combine blocks with less than minimum black between them
	for (i = block_count; i >= 1; i--) {

		unsigned int bfcount = cblock[i].b_head + cblock[i-1].b_tail;

		if (bfcount < min_black_frames_for_break) {
			
			Debug(10, "Combining blocks %i and %i because there are only %i black frames seperating them.\n",
				i-1, i, bfcount);

			cblock[i-1].f_end	= cblock[i].f_end;
			cblock[i-1].b_tail	= cblock[i].b_tail;
			cblock[i-1].length	= (cblock[i-1].f_end - cblock[i-1].f_start) / fps;
			cblock[i-1].cause	= cblock[i].cause;
			
			for (k = i; k < block_count-1; k++) {
				cblock[k]				= cblock[k+1];
			}
			block_count--;
		}
	}
#endif
	return (true);
}


void FindLogoThreshold()
{
	int i;
	int buckets = 20;
	int counter = 0;
	if (framearray) {
		for (i = 1; i < frame_count; i += 1 /*(int) fps */ ) {
			logoHistogram[(int)(frame[i].currentGoodEdge * (buckets - 1))]++;
		}
		
		OutputLogoHistogram(buckets);
		counter = 0;
		for (i = 0; i < buckets; i++) {
			counter += logoHistogram[i];
			if (100 * counter / frame_count > 40)
				break;
		}
		if (i < buckets/2)
			i = buckets * 3 / 4;
		else {
			if (logoHistogram[i - 2] < logoHistogram[i])
				i -= 2;
			if (logoHistogram[i - 1] < logoHistogram[i])
				i -= 1;
			if (logoHistogram[i - 1] < logoHistogram[i])
				i -= 1;
			if (logoHistogram[i - 1] < logoHistogram[i])
				i -= 1;
		}
		logo_quality = ((double) i + 0.5) / (double) buckets;
		Debug(8, "Set Logo Quality = %.5f\n", logo_quality);

/*
		j = 0;		
		for (i = 0; i < buckets/2; i++) {
			j += logoHistogram[i];
		}
		k = 0;		
		for (i = buckets/2; i < buckets; i++) {
			k += logoHistogram[i];
		}
		if (k < j * 1.3) {
			logo_quality = 0.9;
		} else {
			k = logoHistogram[0];
			for (i = 0; i < buckets; i++) {
				if (logoHistogram[k] < logoHistogram[i]) {
					k = i;
				}
			}
			if (k < buckets * 2 / 3) {
				logo_quality = 0.9;
			} else {			
				i = 0;
				j = logoHistogram[0];
				for (i = buckets/2; i < k; i++) {
					if (j * 10 / 8 >= logoHistogram[i]) {
						j = logoHistogram[i];
						logo_quality = ((double) i + 0.5) / (double) buckets;
					}
				}
			}
		}
*/
	}
	if (logo_threshold == 0) {
		logo_threshold = logo_quality;
	}
}

void CleanLogoBlocks()
{
	int i,k,n;
//	double stdev;
	int sum_brightness,v,b, sum_volume,s,sum_silence,sum_uniform;
	double sum_brightness2;
	int sum_delta;
#if 1
	if (commDetectMethod & LOGO &&! reverseLogoLogic && connect_blocks_with_logo) {
		//Combine blocks with both logo
		for (i = block_count-1; i >= 1; i--) {
			if (CheckFrameForLogo(cblock[i-1].f_end) &&
				CheckFrameForLogo(cblock[i].f_start) ) {
				
				Debug(6, "Joining blocks %i and %i at frame %i because they both have a logo.\n",
					i-1, i, cblock[i-1].f_end);
				
				cblock[i-1].f_end	= cblock[i].f_end;
				cblock[i-1].b_tail	= cblock[i].b_tail;
				if (cblock[i].length > cblock[i-1].length)
					cblock[i-1].ar_ratio = cblock[i].ar_ratio;	// Use AR of longest cblock
				cblock[i-1].length	= (cblock[i-1].f_end - cblock[i-1].f_start) / fps;
				cblock[i-1].cause	= cblock[i].cause;
				
				for (k = i; k < block_count-1; k++) {
					cblock[k] = cblock[k+1];
				}
				block_count--;
			}
		}
	}
#endif

	k = -1;
		//Checking cblock size ratio
/*
	for (i = 0; i < block_count; i++) {

		if (cblock[i].f_end - cblock[i].f_start > (int) min_show_segment_length * fps)
		{
			if (k != -1 && i > k+1)
			{
				a = cblock[k].f_end - cblock[k].f_start;
				j = cblock[i].f_start - cblock[k].f_end;
				Debug(1, "Long/Short cblock ratio for cblock %i till %i is %i percent\n",k, i-1 , (int)(100 * a)/(a+j));
			}
			k = i;
		}
	}
*/
	avg_brightness = 0;
	avg_volume = 0;
	maxi_volume = 0;
	avg_silence = 0;
	avg_uniform = 0;
	avg_schange = 0.0;
	for (i = 0; i < block_count; i++) {
		sum_brightness = 0;
		sum_volume = 0;
		sum_silence = 0;
		sum_uniform = 0;
		sum_brightness2 = 0.0;
		sum_delta = 0;
		if (framearray) {
			for (k = cblock[i].f_start+1; k < cblock[i].f_end; k++) {

//				b = frame[k].brightness;
				b = abs(frame[k].brightness - frame[k-1].brightness);
				v = frame[k].volume;
				if (maxi_volume < v)
					maxi_volume = v;
				s = (frame[k].volume < max_volume ? 0 : 99);
				sum_brightness += b;
				sum_volume += v;
				sum_silence += s;
				sum_uniform += abs(frame[k].uniform - frame[k-1].uniform);
				sum_brightness2 += b*b;
				sum_delta += abs(frame[k].brightness - frame[k-1].brightness);
			}
		}
		n = cblock[i].f_end - cblock[i].f_start+1;
		cblock[i].brightness = sum_brightness * 1000 / n;
		cblock[i].volume = sum_volume / n;
		cblock[i].silence = sum_silence / n;
		cblock[i].uniform = sum_uniform / n;

    if ((cblock[i].schange_count = CountSceneChanges(cblock[i].f_start, cblock[i].f_end))) {
			cblock[i].schange_rate = (double)cblock[i].schange_count / n;
		}
		else
			cblock[i].schange_rate = 0.0;

		cblock[i].stdev = 
//			sqrt( (n*sum_brightness2 - sum_brightness*sum_brightness)/ (n * (n-1)));
			100* sum_delta / n;
		avg_brightness += sum_brightness * 1000;
		avg_volume += sum_volume;
		avg_silence += sum_silence;
		avg_uniform += sum_uniform;
		avg_schange += cblock[i].schange_rate*n;
	}
	n = cblock[block_count-1].f_end - cblock[0].f_start;
	avg_brightness /= n;
	avg_volume /= n;
	avg_silence /= n;
	avg_uniform /= n;
	avg_schange /= n;

//	Debug(1, "Average brightness is %i\n",avg_brightness);
//	Debug(1, "Average volume is %i\n",avg_volume);

}

#define LOGO_BORDER 5
void InitScanLines()
{
	int i;
	for (i = 0; i < height; i++) {
		if (i < logoMinY - LOGO_BORDER || i > logoMaxY + LOGO_BORDER) { 
			lineStart[i] = border;
			lineEnd[i] = width-1-border;
		} else {
			if ( logoMinX > width - logoMaxX) { // Most pixels left of the logo
				lineStart[i] = border;
				lineEnd[i] = logoMinX-LOGO_BORDER;
			} else {
				lineStart[i] = logoMaxX+LOGO_BORDER;
				lineEnd[i] = width-1-border;
			}
		}
	}	
	for (i = height; i < MAXHEIGHT; i++) {
		lineStart[i] = 0;
		lineEnd[i] = 0;
	}
}

void InitHasLogo()
{

	int x,y;
	memset(haslogo,MAXWIDTH*MAXHEIGHT,sizeof(int));
	for (y = logoMinY - LOGO_BORDER; y < logoMaxY + LOGO_BORDER; y++) {
		for (x = logoMinX-LOGO_BORDER; x < logoMaxX + LOGO_BORDER ; x++) {
			haslogo[y*width+x] = 1;
		}
	}
}



#define DEBUGFRAMES 80000

#ifdef RGB
#define PIXEL(X,Y) graph[(((oheight+30) -(Y))*owidth+(X))*3+0] = graph[(((oheight+30) -(Y))*owidth+(X))*3+1] = graph[(((oheight+30) -(Y))*owidth+(X))*3+2]
#define SETPIXEL(X,Y,R,G,B) { graph[(((oheight+30) -(Y))*owidth+(X))*3+0] = (B); graph[(((oheight+30) -(Y))*owidth+(X))*3+1] = (G); graph[(((oheight+30) -(Y))*owidth+(X))*3+2] = (R); }

#else
#define PIXEL(X,Y) graph[((Y)*owidth+(X))]
#endif


#define PLOT(S, I, X, Y, MAX, L, R,G,B) { int y, o; o = oheight - (oheight/(S))* (I); y = (Y)*(oheight/(S)-5)/(MAX); if (y < 0) y = 0; if (y > (oheight/(S)-1)) y = (oheight/(S)-1); SETPIXEL((X),(o - y),((Y) < (L) ? 255: R ) , ((Y) < (L) ? 255: G ) ,((Y) < (L) ? 255: B));}

int oheight = 0;
int owidth = 0;
int divider = 1;
int oldfrm = -1;
int zstart = 0;
int zfactor = 1;
int show_XDS=0;


void OutputDebugWindow(bool showVideo, int frm, int grf)
{
#ifdef _WIN32
	int i,j,x,y,a,c,r,s,g,gc,lb=0,e,f,n,bl,xd;
	int v,w;
	int bartop = 0;
	int b,cb;
	int barh = 32;
	int vidtop = 30;
	char t[1024];
	char x1[80];
	char x2[80];
	char x3[80];
	char x4[80];
	char x5[80];
	char *tt[40];
	bool	blackframe, bothtrue, haslogo, uniformframe;
	int silence=0;
//	frm++;
	if (oldfrm == frm)
		return;
	oldfrm = frm;
	if (output_debugwindow && frame_count ) {
		
		if (!vo_init_done) {
			if (width == 0 /*|| (loadingCSV && !showVideo) */)
				width = 800; // MAXWIDTH;
			if (height == 0 /*||  (loadingCSV && !showVideo) */)
				height = 600-barh; // MAXHEIGHT-30;
			if (height > 600 || width > 800) {
				oheight = height / 2;
				owidth = width / 2 ;
				divider = 2;
			} else 
			{
				oheight = height;
				owidth = width;
				divider = 1;
			}
			oheight = (oheight + 31) & -32;
			owidth = (owidth + 31) & -32;
			sprintf(t, windowtitle, filename);
			vo_init(owidth, oheight+barh,t);
//			vo_init(owidth, oheight+barh,"Comskip");
			owidth = owidth;
			vo_init_done++;
		}
//		bartop = oheight;
		if (frm >= frame_count)
			frm = frame_count-1;
		if (frm < 1)
			frm = 1;

		v = frame_count/zfactor;
		
		if ( frm < zstart + v / 10) zstart = frm - v / 10;
		if ( zstart < 0 ) zstart = 0;


		if ( frm > v + zstart - v / 10) zstart = frm - v + v / 10;

		if (zstart + v > frame_count) zstart = frame_count - v;
//		if ( frm > v + zstart) zstart = frm - v;

		w = ((frm - zstart)* owidth / v);



		if (showVideo && frame_ptr) {
#ifdef RGB
			memset(graph, 0, owidth*oheight*3);
/*
			for (x = 0; x < border; x++) {
				for (y = 0; y < oheight; y++) {
					PIXEL(x,y+barh) = 0;
					PIXEL(owidth - 1 - x,y+barh) = 0;
				}
			}
*/
			for (x = 0+border; x < owidth-border; x++) {
//				for (y = 0; y < border; y++) {
//					PIXEL(x,y+barh) = 0;
//					PIXEL(x,oheight - 1 - (y+barh)) = 0;
//				}
				for (y = 0+border; y < oheight-border; y++) {
					if (x*divider < width && y*divider < height)
						PIXEL(x,y+barh) = frame_ptr[(y*divider)*(width)+(x*divider)] >> (grf?1:0);
//					PIXEL(x,y+barh) = min_br[(y*divider)*width+(x*divider)];		//MAXMIN Logo search
										
//					PIXEL(x,y+barh) = vert_edges[(y*divider)*width+(x*divider)];	//Edge detect

//					PIXEL(x,y+barh) = (ver_edgecount[(y*divider)*width+(x*divider)]* 4);		// Edge count			
/*
					PIXEL(x,y+barh) =(abs((frame_ptr[(y*divider)*width+(x*divider)] +
										      frame_ptr[(y*divider)*width+((x+1)*divider)])/2
											  - 
											  (frame_ptr[(y*divider)*width+((x+2)*divider)]+
											  frame_ptr[(y*divider)*width+((x+3)*divider)])/2
										) > edge_level_threshold ? 200 : 0);
*/
					//					graph[((oheight - y)*owidth+x)*3+0] = frame_ptr[y*owidth+x];
					//					graph[((oheight - y)*owidth+x)*3+1] = frame_ptr[y*owidth+x];
					//					graph[((oheight - y)*owidth+x)*3+2] = frame_ptr[y*owidth+x];
				}
			}
			//			memcpy(&graph[owidth*oheight * 0], frame_ptr, owidth*oheight);
			//			memcpy(&graph[owidth*oheight * 1], frame_ptr, owidth*oheight);
			//			memcpy(&graph[owidth*oheight * 2], frame_ptr, owidth*oheight);
#else
			memcpy(&graph[owidth*oheight * 0], frame_ptr, owidth*oheight);
#endif
			if (framearray && grf && ((commDetectMethod & LOGO) || logoInfoAvailable )) {
				if (aggressive_logo_rejection)
					s = edge_radius/2;				// Cater of mask offset
				else
					s = 0;
//				w = 0;
//				v = 0;
				if (logoInfoAvailable) {	// Show logo mask 
					if (frame[frm].currentGoodEdge > logo_threshold) {
						e = (int)(frame[frm].currentGoodEdge * 250);
						for (y = logoMinY/divider; y < (logoMaxY+1)/divider; y++) {
							for (x = logoMinX/divider; x < (logoMaxX+1)/divider; x++) {
								if (horiz_edgemask[(y*divider) * width + (x*divider)]) r = 255; else r = 0;
								if (vert_edgemask[(y*divider) * width + (x*divider)]) g = 255; else g = 0;
								if (r || g) SETPIXEL(x-s,y-s+barh,r,g,0);
							}
						}
					}
				} else {					// Show detected logo pixels only while scanning input
					if (frm+1 == frame_count) {
						for (y = s; y < oheight; y++) {
							for (x = s ; x < owidth; x++) {
								if (hor_edgecount[(y*divider) * width + (x*divider)] >= num_logo_buffers*2/3) r = 255; else r = 0;
								if (ver_edgecount[(y*divider) * width + (x*divider)] >= num_logo_buffers*2/3) g = 255; else g = 0;
								if (r || g) SETPIXEL(x-s,y-s+barh,r,g,0);
							}
						}
						
					}
					
				}
				
			}
		}
		else
		{
			memset(graph, 0, owidth*oheight*3);

		}	
		
		
		if (framearray && grf) {
				for (y=0; y < oheight; y++) {
					SETPIXEL(w,y,100,100,100);
				}
				bl = 0;
				for (x=0 ; x < owidth; x++) {				// debug bar
					a = 0;
					b = 0;
					s = 0;
					c = 0;
					n = 0;
					if (cblock && grf == 2) {
					while (bl < block_count && cblock[bl].f_end < zstart+(int)((double)x * v /owidth))
						bl++;
#define PLOTS	9

					PLOT(PLOTS, 0, x, cblock[bl].brightness, 2550, (int)(avg_brightness*punish_threshold), 0, 255, 0); // RED
					PLOT(PLOTS, 1, x, cblock[bl].volume/100, 100000, (int)(avg_volume*punish_threshold)/100, 255, 0, 0); // RED
					PLOT(PLOTS, 2, x, cblock[bl].uniform, 3000, (int)(avg_uniform*punish_threshold), 255, 0,0); // RED
					PLOT(PLOTS, 3, x, (int)(cblock[bl].schange_rate*1000), 1000, (int)(avg_schange*punish_threshold*1000), 255, 0, 0);	// PURPLE
					}
					for (i = zstart+(int)((double)x * v /owidth); i < zstart+(int)((double)(x+1) * v /owidth ); i++) {
						if (i <= frame_count) {
							b += frame[i].brightness;
							PLOT(PLOTS, 0, x, frame[i].brightness, 255, max_avg_brightness, 255, 0,0); // RED
							s += frame[i].volume;
							PLOT(PLOTS, 1, x, frame[i].volume, 10000, max_volume, 0, 255, 0);			// GREEN
							e += frame[i].uniform;
							PLOT(PLOTS, 2, x, frame[i].uniform, 30000, non_uniformity, 0, 255, 255);	// LIGHT BLUE
							c += (int)(frame[i].currentGoodEdge*100);
							PLOT(PLOTS, 4, x, (int)(frame[i].currentGoodEdge*100), 100, 0, 255, 255, 0);  // YELLOW
							PLOT(PLOTS, 4, x, (int)(frame[i].logo_filter*50+50), 100, 0, (frame[i].logo_filter < 0.0 ?255:0) , (frame[i].logo_filter < 0.0 ?0:255), 0);
							PLOT(PLOTS, 5, x, (int)((frame[i].ar_ratio-0.5) * 100), 250, 0, 0, 0, 255);   // BLUE

							if (commDetectMethod & CUTSCENE)
								{ PLOT(PLOTS, 3, x, (int)(frame[i].cutscenematch), 100, cutscenedelta, 255, 0, 255); }	// PURPLE
							else
							{ PLOT(PLOTS, 3, x, (int)(frame[i].schange_percent), 100, schange_cutlevel, 255, 0, 255);	} // PURPLE
							a += frame[i].maxY;
							PLOT(PLOTS, 6, x, frame[i].maxY, height, 0, 0, 128, 128); 
							b += frame[i].minY;
							PLOT(PLOTS, 6, x, frame[i].minY, height, 0, 0, 128, 128); 
							n++;
							PLOT(PLOTS, 7, x, frame[i].maxX, width, 0, 0, 0, 255); 
							PLOT(PLOTS, 7, x, frame[i].minX, width, 0, 0, 0, 255); 
						}
					}
					if (n > 0) {
						a /= n;
//						f /= n;
						b /= n;
						s /= n;
						c /= n;
						e /= n;
					}
// PLOT(S, I, X, Y, MAX, L, R,G,B)
				}
			}
		


		if (frame_ptr && framearray) {
//			for (x=0; x < owidth; x++) { // Edge counter indicator
//				graph[2* owidth + x] = (x < edge_count /8 ? 255 : 0);
//			}

			x = frame[frm].maxX/divider;
			if (x == 0) x = owidth;
			for (i=frame[frm].minX/divider; i < x; i++) {				// AR lines
				SETPIXEL(i, (frame[frm].minY/divider)+barh, 0,0,255);
				SETPIXEL(i, (frame[frm].maxY/divider)+barh, 0,0,255);

//				graph[frame[frm].minY* owidth + i] = 255;
//				graph[frame[frm].maxY* owidth + i] = 255;
			}
			for (i=(frame[frm].minY/divider); i < (frame[frm].maxY/divider); i++) {				// AR lines
				SETPIXEL((frame[frm].minX/divider), i+barh, 0,0,255);
				SETPIXEL((frame[frm].maxX/divider), i+barh, 0,0,255);
			}

		
		}
		if (framearray /* && commDetectMethod & LOGO */ ) {
			
			for (x = logoMinX/divider; x < logoMaxX/divider; x++) {		// Logo box X
				SETPIXEL(x,logoMinY/divider+barh,255,e,e);
				SETPIXEL(x,logoMaxY/divider+barh,255,e,e);
			}			
			for (y = logoMinY/divider; y < logoMaxY/divider; y++) {		// Logo box Y
				SETPIXEL(logoMinX/divider,y+barh,255,e,e);
				SETPIXEL(logoMaxX/divider,y+barh,255,e,e);
			}			
		}

		b = 0;
		for (i = 0; i < block_count; i++) {
			if (cblock[i].f_start <= frm && frm <= cblock[i].f_end) {
				b = i;
				break;
			}
		}

/*
		memset(graph,20,owidth*(oheight+30)*3);
		for (i=0; i<oheight/2;i++) {
			graph[(i*(owidth+0))*3] = 255;
			graph[(i*(owidth+0))*3+1] = 0;
			graph[(i*(owidth+0))*3+2] = 0;
		}		
*/
//		if (0)	// disable debug bar
		for (x=0 ; x < owidth; x++) {				// debug bar
			blackframe = false;
			uniformframe = false;
			silence = 0;
			haslogo = false;
			bothtrue = false;
			g = 0;
			gc = 0;
			xd = 0;
//			v = max(frame_count, DEBUGFRAMES);	
			if (framearray) {
				xd = XDS_block_count-1;
				while (xd > 0 && XDS_block[xd].frame > zstart+(int)((double)(x+1) * v /owidth) )
					xd--;
				if (xd > 0 && XDS_block[xd].frame >= zstart+(int)((double)x * v /owidth))
					xd = xd;
				else
					xd = 0;

			for (i = zstart+(int)((double)x * v /owidth); i < zstart+(int)((double)(x+1) * v /owidth ); i++) {
				if (i <= frame_count) {
					if ((frame[i].isblack & C_b) || (frame[i].isblack & C_r)) {
						blackframe = true;
						for (j = 0; j < block_count; j++) {
							if (cblock[j].f_end == i)
								bothtrue = true;
						}
					}
					if (frame[i].isblack & C_u) {
						uniformframe = true;
					}
					if (frame[i].volume < max_volume && silence < 1) silence = 1;
					if ((frame[i].volume < 50 || frame[i].volume < max_silence) && silence < 2) silence = 2;
					if ((frame[i].isblack & C_v)) silence = 3;
					if ((frame[i].isblack & C_b) && frame[i].volume < max_volume) bothtrue = true;
					if ((frame[i].isblack & C_r)) bothtrue = true;
					if (frm+1 == frame_count) {						// Show details of logo while scanning
						if (frame[i].logo_present) haslogo = true;
					} else {  
						while (lb < logo_block_count && i > logo_block[lb].end)			// Show logo blocks when finished
							lb++;
						if (lb < logo_block_count && i >= logo_block[lb].start) {
							haslogo=true;						
						}
					}
//					if (frame[i].currentGoodEdge > logo_threshold) haslogo = true;
					a = (int)((frame[i].ar_ratio - 0.5 - 0.1)*6);		// Position of AR line
					g += (int)(frame[i].currentGoodEdge * 5);
					gc++;
				}
			}
			}
			if (gc > 0)
				g /= gc;
			c = 255;
			if (c == 255) {
				for (i = 0; i <= commercial_count; i++) {	// Inside commercial?
					if (zstart+(int)((double)x * v /owidth ) >= commercial[i].start_frame &&
						zstart+(int)((double)x * v /owidth ) <= commercial[i].end_frame ) {
						c = 128;
						break;
					}
				}
			}

			if (c == 255) {									// not in a commercial but score above threshold
				for (i = 0; i < block_count; i++) {
					if (zstart+(int)((double)x * v /owidth ) >= cblock[i].f_start && 
						zstart+(int)((double)x * v /owidth ) <= cblock[i].f_end &&
						cblock[i].score > global_threshold ) {
						c = 220;
						break;
					}
				}
			}
			
			
			r = 255;
			for (i = 0; i <= reffer_count; i++) {		// Inside reference?
				if (zstart+(int)((double)x * v /owidth ) >= reffer[i].start_frame &&
					zstart+(int)((double)x * v /owidth ) <= reffer[i].end_frame ) {
					r = 0;
					break;
				}
			}

			a = bartop + 14 - a;
			for (y = bartop+5; y < bartop+15 ; y++)			// Commercial / AR bar
				if (y == a) {
					SETPIXEL(x,y,0,0,255); 
				} else {
					SETPIXEL(x,y,c,c,c); 
//					PIXEL(x,y) = c;
				}
			g = 5; // Disable goodEdge graph
			for (i = 0; i < block_count; i++) {
				if (zstart+(int)((double)x * v /owidth ) >= cblock[i].f_start && 
					zstart+(int)((double)x * v /owidth ) <= cblock[i].f_end &&
					cblock[i].correlation > 0 ) {					// if inside a correlated cblock
					g=2;
					break;
				}
			}
			for (y = bartop + 15; y < bartop+20 ; y++) {		// Logo bar

				if (haslogo) PIXEL(x,y) = ((y - (bartop + 15) == g)?255:((commDetectMethod & LOGO)? 0 : 128)); 
				else PIXEL(x,y) = ((y - (bartop + 15) == g)?0:255);
//				if (y - (bartop + 15) == g) graph[y * owidth + x] = 128;
			}

			cb = 255;
			if (cblock != 0 && cblock[b].f_start <= zstart+(int)((double)x * v /owidth ) && zstart+(int)((double)x * v /owidth ) <= cblock[b].f_end)
				cb = 0;

			if (bothtrue)
				c = 0;
			else
				c = 255;
			for (y = bartop + 20; y < bartop+25 ; y++)      // Blackframe bar
			{
				if (blackframe) 
					{ SETPIXEL(x,y,c,0,0);} 
				else
				if (uniformframe)
					{ SETPIXEL(x,y,0,0,c);} 
				else 
					{ SETPIXEL(x,y,255,cb,255); }
			}
			c = 255;

			for (y = bartop + 25; y < bartop+30 ; y++)		// Silence bar
			{
				if (silence == 1) { SETPIXEL(x,y,0,c,0);} 
				else 
				if (silence == 2) { SETPIXEL(x,y,0,0,c);} 
				else 
				if (silence == 3) { SETPIXEL(x,y,c,0,0);} 
				else 
					PIXEL(x,y) = 255;
			}
			if (w < owidth)									// Progress indicator
				for (y = bartop; y < bartop+30 ; y++) SETPIXEL(w,y,255,0,0);
			for (y = bartop; y < bartop+(loadingTXT?20:5) ; y++)
			{	// Reference bar
				if (xd) {
					SETPIXEL(x,y,128,128,128);
				} else
					if (reffer_count >= 0) PIXEL(x,y) = r;
			}
		}
		vo_draw(graph);
		
		//		sprintf(t, "%8i %8i %1s %1s", frm, framenum_infer, (frame[frm].isblack?"B":" "), (frame[frm].volume<max_volume?"S":" "));
		b = 0;
		for (i = 0; i < block_count; i++) {
			if (cblock[i].f_start <= frm && frm <= cblock[i].f_end) {
				b = i;
				break;
			}
		}
		if (recalculate) {
			sprintf(t, "max_volume=%d, non_uniformity=%d, max_avg_brightness=%d", max_volume, non_uniformity, max_avg_brightness);
		}
		else
		{
		if (framearray) {
			if (b < block_count)
				sprintf(t, "%8i B=%i%1s V=%i%1s U=%i%1s AR=%4.2f  Block #%i Length=%6.2fs Score=%6.2f Logo=%6.2f %s                      ", frm, frame[frm].brightness, (frame[frm].isblack & C_b?"B":" "), frame[frm].volume, (frame[frm].volume<max_volume?"S":" "),frame[frm].uniform, (frame[frm].uniform<non_uniformity?"U":" "), frame[frm].ar_ratio, b, cblock[b].length, cblock[b].score, cblock[b].logo, CauseString(cblock[b].cause)
				);
			else
				sprintf(t, "%8i B=%i%1s V=%i%1s U=%i%1s AR=%4.2f                                                                          ", frm, frame[frm].brightness, (frame[frm].isblack & C_b?"B":" "), frame[frm].volume, (frame[frm].volume<max_volume?"S":" "),frame[frm].uniform, (frame[frm].uniform<non_uniformity?"U":" "), frame[frm].ar_ratio);
		}
		else
			sprintf(t, "%8i", frm);
		}
		if (soft_seeking) {
			tt[0] = t;
			tt[1] = "WARNING: Seeking inaccurate, do not use for cutpoint review!";
			tt[2] = 0;
			ShowHelp(tt);
		} else if (helpflag) 
			ShowHelp(helptext);
		else if (show_XDS) {
			tt[0] = t;
			i = (XDS_block_count > 0 ? XDS_block_count-1 : 0);
			while (i > 0 && XDS_block[i].frame > frm)
				i--;
			sprintf(x1,"Program Name    : %s", XDS_block[i].name);
			tt[1] = x1;
			sprintf(x2,"Program V-Chip  : %4x", XDS_block[i].v_chip);
			tt[2] = x2;
			sprintf(x3,"Program duration: %2d:%02d", (XDS_block[i].duration & 0x3f00)/256, (XDS_block[i].duration & 0x3f) % 256);
			tt[3] = x3;
			sprintf(x4,"Program position: %2d:%02d", (XDS_block[i].position & 0x3f00)/256, (XDS_block[i].position & 0x3f) % 256);
			tt[4] = x4;
			sprintf(x5,"Composite Packet: %2d:%02d, %2d/%2d, ", (XDS_block[i].composite1 & 0x3f00)/256, (XDS_block[i].composite1 & 0x1f) % 256, (XDS_block[i].composite2 & 0x1f00)/256, (XDS_block[i].composite2 & 0x0f) % 256);
			tt[5] = x5;
			tt[6] = 0;
			ShowHelp(tt);
		}
		else 
			ShowDetails(t);
	}
	if (key == 0x20) {
		subsample_video  = 0;
		key = 0;
	}
	if (key == 27) {
		exit(1);
	}
	if (key == 'G') {
		subsample_video  = 0x3f;
		key = 0;
	}
	if (subsample_video == 0)
	{
	//	Enable for single stepping trough the video
		if (!vo_init_done) {
			if (width == 0 /*|| (loadingCSV && !showVideo) */)
				width = 800; // MAXWIDTH;
			if (height == 0 /*||  (loadingCSV && !showVideo) */)
				height = 600-barh; // MAXHEIGHT-30;

			if (height > 600 || width > 800) {
				oheight = height / 2;
				owidth = width / 2;
				divider = 2;
			} else {
				oheight = height;
				owidth = width;
				divider = 1;
			}
			owidth = (owidth + 31) & -32;
			sprintf(t, windowtitle, filename);
			vo_init(owidth, oheight+barh,t);
//			vo_init(owidth, oheight+barh,"Comskip");
			owidth = owidth;
			vo_init_done++;
		}

		while(key == 0)
			vo_draw(graph);
		if (key == 27) {
			exit(1);
		}
		if (key == 'G') {
			subsample_video  = 0x3f;
		}
		key = 0;
	}

	recalculate = 0;
#endif
}

static int shift = 0;

void Recalc()
{
	BuildBlocks(true);
	if (commDetectMethod & LOGO) {
		PrintLogoFrameGroups();
	}
	WeighBlocks();
	
	OutputBlocks();
}

bool ReviewResult() {
	FILE *review_file = NULL;
	int curframe = 1;
	int bartop = 0;
	int grf = 2;
	int i,j;
	long prev;
	int b = -1;
	char tsfilename[MAX_PATH];
	if (!framearray) grf = 0;
	output_demux = 0;
	output_data = 0;
	output_srt = 0;
	output_smi = 0;
	if (!review_file && mpegfilename[0])
		review_file = fopen(mpegfilename, "rb");
	if (review_file == 0 ) {
		strcpy(tsfilename, mpegfilename);
		i = strlen(tsfilename);
		while (i > 0 && tsfilename[i-1] != '.') i--;
		tsfilename[i] = 't';		
		tsfilename[i+1] = 's';		
		tsfilename[i+2] = 0;		
		review_file = fopen(tsfilename, "rb");
		if (review_file) {
			demux_pid = 1;
			strcpy(mpegfilename, tsfilename);
		}
	}
	if (review_file == 0 ) {
		strcpy(tsfilename, mpegfilename);
		i = strlen(tsfilename);
		while (i > 0 && tsfilename[i-1] != '.') i--;
		strcpy(&tsfilename[i], "dvr-ms");
		review_file = fopen(tsfilename, "rb");
		if (review_file) {
			demux_asf = 1;
			strcpy(mpegfilename, tsfilename);
		}
	}
	while(true) {
			if (key != 0) {
				if (key == 27) if (!helpflag) exit(0);
				if (key == 112) { helpflag = 1; oldfrm = -1; }
				else { if (helpflag == 1) { helpflag = 0; oldfrm = -1; } }
				if (key == 16) { shift = 1;} 
				if (key == 37) curframe -= 1;
				if (key == 39) curframe += 1;
				if (key == 38) curframe -= (int)fps;
				if (key == 40) curframe += (int)fps;
				if (key == 33) curframe -= (int)(20*fps);
				if (key == 34) curframe += (int)(20*fps);

				if (key == 78 || (key == 39 && shift)) { // Next key
					curframe += 5;
					if (framearray) {
						i = 0;
						while (i <= commercial_count && curframe > commercial[i].end_frame) i++;
	//					if (i > 0)
							curframe = commercial[i].end_frame+5;
//						while (curframe < frame_count && frame[curframe].isblack) curframe++;
//						while (curframe < frame_count && !frame[curframe].isblack) curframe++;
	//					while (curframe < frame_count && frame[curframe].isblack) curframe++;
					} else {
						i = 0;
						while (i <= reffer_count && curframe > reffer[i].end_frame) i++;
	//					if (i > 0)
							curframe = reffer[i].end_frame+5;
					}
					curframe -= 5;
				}
				if (key == 80 || (key == 37 && shift)) {	// Prev key
					curframe -= 5;
					if (framearray) {
						i = commercial_count;
						while (i >= 0 && curframe < commercial[i].start_frame) i--;
	//					if (i > 0)
							curframe = commercial[i].start_frame-5;
	//					while (curframe > 1 && frame[curframe].isblack) curframe--;
	//					while (curframe > 1 && !frame[curframe].isblack) curframe--;
	//					while (curframe > 1 && frame[curframe].isblack) curframe--;
					} else {
						i = reffer_count;
						while (i >= 0 && curframe < reffer[i].start_frame) i--;
	//					if (i > 0)
							curframe = reffer[i].start_frame-5;
					}
					curframe += 5;
				}
				if (key == 'S') {
					if (framearray) {
						curframe = 0;
					} else {
						curframe = 	0;

					}
				}
				if (key == 'F') {
					if (framearray) {
						curframe = frame_count;
					} else {
						curframe = 	frame_count;

					}
				}
				if (key == 'E') {	// End key
					if (framearray) {
						curframe += 10;
						i = 0;
						while (i < block_count && curframe > cblock[i].f_end) i++;
	//					if (i > 0)
							curframe = cblock[i].f_end+5;
						curframe -= 10;
					} else {
						i = reffer_count;
						while (i >= 0 && curframe < reffer[i].start_frame) i--;
						if (i >= 0)
							reffer[i].end_frame = curframe;
						oldfrm = -1;
					}
				}
				if (key == 'B') {	// begin key
					if (framearray) {
						curframe -= 10;
						i = block_count-1;
						while (i > 0 && curframe < cblock[i].f_start) i--;
	//					if (i > 0)
							curframe = cblock[i].f_start-5;
						curframe += 10;
					} else {
						i = 0;
						while (i <= reffer_count && curframe > reffer[i].end_frame) i++;
						if (i <= reffer_count)
							reffer[i].start_frame = curframe;
						oldfrm = -1;
					}
				}
				if (key == 'T') {	// Toggle key
					if (framearray) {
						i = 0;
						while (i < block_count && curframe > cblock[i].f_end) i++;
						if (i < block_count) {
							if (cblock[i].score < global_threshold)
								cblock[i].score = 99.99;
							else
								cblock[i].score = 0.01;
							cblock[i].cause |= C_F;
							oldfrm = -1;
							BuildCommercial();
							key = 'W';			// Trick to cause writing of the new commercial list
						}
					}
				}
				if (key == 68) {	// Delete key
					if (framearray) {
						i = 0;
						while (i < block_count && curframe > cblock[i].f_end) i++;
						if (i < block_count) {
							cblock[i].score = 99.99;
							cblock[i].cause |= C_F;
							oldfrm = -1;
							BuildCommercial();
						}
					} else {
						i = reffer_count;
						while (i >= 0 && curframe < reffer[i].start_frame) i--;
						if (i >= 0 && reffer[i].start_frame <= curframe && curframe <= reffer[i].end_frame ) {
							while (i < reffer_count) {
								reffer[i] = reffer[i+1];
								i++;
							}
							reffer_count--;						
							oldfrm = -1;
						}
					}
				}
				if (key == 73) {	// Insert key
					if (framearray) {
						i = 0;
						while (i < block_count && curframe > cblock[i].f_end) i++;
						if (i < block_count) {
							cblock[i].score = 0.01;
							cblock[i].cause |= C_F;
							oldfrm = -1;
							BuildCommercial();
						}
					} else {
						i = reffer_count;
						while (i >= 0 && curframe < reffer[i].start_frame) i--;
						if (i == -1 || curframe > reffer[i].end_frame ) { //Insert BEFORE i
							j = reffer_count;
							while (j > i) {
								reffer[j+1] = reffer[j];
								j--;
							}
							reffer[i+1].start_frame = max(curframe-1000,1);
							reffer[i+1].end_frame = min(curframe+1000,frame_count);
							reffer_count++;						
							oldfrm = -1;
						}
					}
				}
				if (key == 'W') { // W key
					output_default = true;
					OpenOutputFiles();
					if (framearray) {
						prev = -1;
						for (i = 0; i <= commercial_count; i++) {
							OutputCommercialBlock(i, prev, commercial[i].start_frame, commercial[i].end_frame, (commercial[i].end_frame < frame_count-2 ? false : true));
							prev = commercial[i].end_frame;
						}
						if (commercial[commercial_count].end_frame < frame_count-2)
							OutputCommercialBlock(commercial_count, prev, frame_count-2, frame_count-1, true);
					} else { 
						prev = -1;
						for (i = 0; i <= reffer_count; i++) {
							OutputCommercialBlock(i, prev, reffer[i].start_frame, reffer[i].end_frame, (reffer[i].end_frame < frame_count-2 ? false : true));
							prev = reffer[i].end_frame;
						}
						if (reffer[reffer_count].end_frame < frame_count-2)
							OutputCommercialBlock(reffer_count, prev, frame_count-2, frame_count-1, true);
					}
					output_default = false;
					oldfrm = -1;
				}
				if (key == 'Z') {
					if (zfactor < 256 && frame_count / zfactor > owidth) {
//						i = (curframe - zstart) * zfactor * owidth/ frame_count;
						zfactor = zfactor << 1;
//						zstart = i * frame_count / owidth / zfactor; 
						zstart = (curframe + zstart) / 2;
						oldfrm = -1;
					}
				}
				if (key == 'U') {
					if (zfactor > 1) {
//						i = (curframe - zstart) * zfactor * owidth/ frame_count;
						zfactor = zfactor >> 1;
//						zstart = i * frame_count / owidth / zfactor; 
						zstart = zstart - (curframe - zstart);
						if (zstart < 0)
							zstart = 0;
						oldfrm = -1;

					}
				}
				if (key == 'C') {
					RecordCutScene(curframe, frame[curframe].brightness);
				}

				if (key == 'X') {
					show_XDS = !show_XDS;
					oldfrm = -1;
				}

				if (key == 'G') {
					grf++;
					if (grf > 2)
						grf = 0;
					oldfrm = -1;
				}
				if (key == 113) {				// F2 key
					max_volume = (int)(max_volume / 1.1);
					Recalc();
					oldfrm = -1;
				}
				if (key == 114) {				// F3 key
					non_uniformity = (int)(non_uniformity / 1.1);
					Recalc();
					oldfrm = -1;
				}
				if (key == 115) {				// F4 key
					max_avg_brightness = (int)(max_avg_brightness / 1.1);
					Recalc();
					oldfrm = -1;
				}
				if (key == '.') {
					oldfrm = -1;
				}
				if (key == 82) return(true);
				if (key != 16) shift = 0;
				key = 0;
			}
			if (lMouseDown) {
				if (yPos >= bartop && yPos < bartop + 30)
					curframe = zstart+frame_count * xPos / owidth / zfactor + 1;
				lMouseDown = 0;
			}
			if (curframe < 1) curframe = 1;
			if (frame_count > 0) {
				if (curframe >= frame_count) curframe = frame_count-1;
			}
			if (frame_count > 0 && review_file)
				DecodeOnePicture(review_file, curframe);
			OutputDebugWindow((review_file ? true : false),curframe, grf);
#ifndef GUI			
				vo_wait();
//				vo_refresh();
//				Sleep((DWORD)100);
#endif
	}
	return false;
}


#define FRAMENO_FROM_PTS(P) ( (unsigned int)    ((P) / (int)(90000/fps))  )

int DetectCommercials(int f) {
	bool isBlack = 0;	/*Gil*/
	int i,j;
	bool	logoAppeared = false;
	long oldBlack_count;


	if (loadingTXT)
		return(0);
	if (loadingCSV)
		return(0);
	if (!initialized) 
		InitComSkip();
//	frame_count++;
	frame_count = framenum_real = framenum+1;

//Debug(1, "Frame info f=%d, framenum=%d, framenum_real=%d, frame_count=%d\n",f, framenum, framenum_real, frame_count, max_frame_count);
	
	
	if (framenum_real < 0) return 0;
	if (play_nice) Sleep(play_nice_sleep);
	if (framearray) InitializeFrameArray(framenum_real);
//	curvolume = RetreiveVolume(framenum_real);
	curvolume = RetreiveVolume(frame_count);
//	if (frame_count != framenum_real)
//		Debug(0, "Inconsistent frame numbers\n");
	if (framearray) {
		frame[frame_count].volume = curvolume;
		frame[frame_count].goppos = headerpos;
		frame[frame_count].pts = pts;
/*		
		if (frame_count > 0 && FRAMENO_FROM_PTS(frame[frame_count].pts) != FRAMENO_FROM_PTS(frame[frame_count-1].pts)+1) {
			missing_frames = FRAMENO_FROM_PTS(frame[frame_count].pts) - FRAMENO_FROM_PTS(frame[frame_count-1].pts) - 1;
			if (missing_frames > 0 && missing_frames < 20) {
				Debug(1, "Video PTS jumped %d frames, repairing timeline\n", missing_frames);
				while (missing_frames-- > 0) {
					frame[frame_count] = frame[frame_count-1];
					frame[frame_count].volume = RetreiveVolume(frame_count);
					framenum_real++;
					frame_count++;
					framenum++;
					InitializeFrameArray(framenum_real);
				}
				frame[frame_count].volume = curvolume;
				frame[frame_count].goppos = goppos;
				frame[frame_count].pts = pts;
			} else 
				Debug(1, "Video PTS jumped %d frames, frame numbers may be incorrect\n", missing_frames);

		}
*/
	}
	if (curvolume > 0) {
			volumeHistogram[(curvolume/volumeScale < 255 ? curvolume/volumeScale : 255)]++;
	}
	if (ticker_tape) {
		memset(&frame_ptr[width*(height - ticker_tape)], 0, width*ticker_tape);
	}
	if (ignore_side) {
		for (i = 0; i < height; i++) {
			for (j = 0; j < ignore_side; j++) {
				frame_ptr[width*i + j] = 0;
				frame_ptr[width*i + (width -1) - j] = 0;
			}
		}
	}
	
	oldBlack_count = black_count;	/*Gil*/
	CheckSceneHasChanged();
	isBlack = oldBlack_count != black_count;	/*Gil*/


	if ((commDetectMethod & LOGO) && ((frame_count % (int)(fps * logoFreq)) == 0)) {
		if (!logoInfoAvailable) {
			if (delay_logo_search == 0 ||
				(delay_logo_search == 1 && frame_count > added_recording * fps * 60) ||
				(frame_count > delay_logo_search * fps))
			{
			FillLogoBuffer();
			if (logoBuffersFull) {
				Debug(6, "\nLooking For Logo in frames %i to %i.\n", logoFrameNum[oldestLogoBuffer], frame_count);
				if(!SearchForLogoEdges()) {
					InitComSkip();
					return 1;
				}
			}
			if (logoInfoAvailable) {
				logoTrendCounter = num_logo_buffers;
				lastLogoTest = true;
				curLogoTest = true;
//				logoTrendStartFrame = logoFrameNum[oldestLogoBuffer];
				curLogoTest = true;
//				lastRealLogoChange = logoFrameNum[oldestLogoBuffer];
//				if (num_logo_buffers >= minHitsForTrend) {
//					hindsightLogoState = true;
//				} else {
//					hindsightLogoState = false;
//				}
			}
			}
		} else {
//			EdgeCount(frame_ptr);
//			curLogoTest = logoBuffersFull;
			currentGoodEdge = CheckStationLogoEdge(frame_ptr);
			curLogoTest = (currentGoodEdge > logo_threshold);
			lastLogoTest = ProcessLogoTest(frame_count, curLogoTest, false);
			if (!loadingCSV && !secondLogoSearch && logo_block_count > 0 && 
				!lastLogoTest &&
				(frame_count - logo_block[logo_block_count-1].end) > (int)( max_commercialbreak * fps * 1.2 ) &&
				(double)frames_with_logo / (double)frame_count < 0.5
				) {
				Debug(6, "\nNo Logo in frames %i to %i, restarting Logo search.\n", logo_block[logo_block_count-1].end, frame_count);
				// First logo found but no logo found after first commercial cblock so search new logo
				logoInfoAvailable = false;
				secondLogoSearch = true;
				logoBuffersFull = false;
				InitLogoBuffers();
				newestLogoBuffer = -1;
			}
		}
	}

//	EdgeCount(frame_ptr);
//	currentGoodEdge = ((double) edge_count) / 750;

	if (framearray) frame[frame_count].logo_present = lastLogoTest;
	if (lastLogoTest)
		frames_with_logo++;
	if (framearray) frame[frame_count].currentGoodEdge = currentGoodEdge;
 /*

  #define graph frame_ptr
		
		graph[gy*width + 0] = 100;
		graph[gy*width + (frame[frame_count].brightness/2)] = 250;
		graph[gy*width + 100] = 100;
		graph[gy*width + 100 + (frame[frame_count].minY/6)] = 250;
		graph[gy*width + 100 + (frame[frame_count].maxY/6)] = 250;
		graph[gy*width + 200] = 100;
		graph[gy*width + 200 + (int)(frame[frame_count].currentGoodEdge * 250)] = 250;
		graph[gy*width + 300] = 100;

		gy++;
		if (gy >= height) gy = 0;
		for (i=0; i < width; i++) {
			graph[gy* width + i] = 0;
		}
*/

	if (((frame_count) & subsample_video) == 0)
		OutputDebugWindow(true,frame_count,true);
//	key = 0;
//	while (key==0)
//		vo_wait();

	framesprocessed++;
	scr += 1;

	if (live_tv && !isBlack) {
		BuildCommListAsYouGo();
	}

	return 0;
}



int Max(int i,int j)
{
	return(i>j?i:j);
}

int Min(int i,int j)
{
	return(i<j?i:j);
}


double AverageARForBlock(int start, int end)
{
	int i, maxSize;
	double Ar;
	int f,t;

	maxSize = 0;
	Ar = 0.0;
	for (i = 0; i < ar_block_count; i++) {
		f = max(ar_block[i].start, start);
		t = min(ar_block[i].end, end);
		if (maxSize < t-f+1) {
			Ar = ar_block[i].ar_ratio;
			maxSize = t-f+1;
		}
		if (ar_block[i].start > end)
			break;
	}
	return(Ar);
}

double	FindARFromHistogram(double ar_ratio)
{
	int i;
	for (i = 0; i < MAX_ASPECT_RATIOS; i++) {
		if (ar_ratio > ar_histogram[i].ar_ratio - ar_delta &&
			ar_ratio < ar_histogram[i].ar_ratio + ar_delta)
			return (ar_histogram[i].ar_ratio);
	}
	for (i = 0; i < MAX_ASPECT_RATIOS; i++) {
		if (ar_ratio > ar_histogram[i].ar_ratio - 2*ar_delta &&
			ar_ratio < ar_histogram[i].ar_ratio + 2*ar_delta)
			return (ar_histogram[i].ar_ratio);
	}
	for (i = 0; i < MAX_ASPECT_RATIOS; i++) {
		if (ar_ratio > ar_histogram[i].ar_ratio - 4*ar_delta &&
			ar_ratio < ar_histogram[i].ar_ratio + 4*ar_delta)
			return (ar_histogram[i].ar_ratio);
	}
	return (0.0);






}

void FillARHistogram(bool refill)
{
	int		i;
	bool	hadToSwap;
	long	tempFrames;
	double	tempRatio;
	long	totalFrames = 0;
	long	tempCount;
	long	counter;
	int		hi;
	
	if (refill) {

		for (i = 0; i < MAX_ASPECT_RATIOS; i++) {
			ar_histogram[i].frames = 0;
			ar_histogram[i].ar_ratio = 0.0;
		}

		for (i = 0; i < ar_block_count; i++) {
			hi = (int)((ar_block[i].ar_ratio - 0.5)*100);
			if (hi >= 0 && hi < MAX_ASPECT_RATIOS) {
				ar_histogram[hi].frames += ar_block[i].end - ar_block[i].start + 1;
				ar_histogram[hi].ar_ratio = ar_block[i].ar_ratio;
			}
		}
	}

	counter = 0;
	do
	{
		hadToSwap = false;
		counter++;
		for (i = 0; i < MAX_ASPECT_RATIOS - 1; i++) {
			if (ar_histogram[i].frames < ar_histogram[i + 1].frames) {
				hadToSwap = true;
				tempFrames = ar_histogram[i].frames;
				tempRatio  = ar_histogram[i].ar_ratio;
				ar_histogram[i] = ar_histogram[i + 1];
				ar_histogram[i+1].frames = tempFrames;
				ar_histogram[i+1].ar_ratio = tempRatio;
			}
		}
	} while (hadToSwap);

	for (i = 0; i < MAX_ASPECT_RATIOS; i++) {
		totalFrames += ar_histogram[i].frames;
	}

	tempCount = 0;
	Debug(10, "\n\nAfter Sorting - %i\n--------------\n", counter);
	i = 0;
	while (i < MAX_ASPECT_RATIOS && ar_histogram[i].frames > 0) {
		tempCount += ar_histogram[i].frames;
		Debug(10, "Aspect Ratio  %5.2f found on %6i frames totalling \t%3.1f%c\n", ar_histogram[i].ar_ratio, ar_histogram[i].frames, ((double)tempCount / (double)totalFrames)*100,'%');
		i++;
	}

}


void InsertBlackFrame(int f, int b, int u, int v, int c)
{
	int i;
	
	//		if ((black_count==0 || black[black_count-1].frame < logo_block[logo_block_count-1].end )) {
	
	i = 0;
	while (i < black_count && black[i].frame != f)
		i++;
	
	if (i < black_count && black[i].frame == f) {
		black[i].cause |= c;
	} else {
		if (black_count >= max_black_count) {
			max_black_count += 500;
			black = realloc(black, (max_black_count + 1) * sizeof(black_frame_info));
			Debug(9, "Resizing black frame array to accomodate %i frames.\n", max_black_count);
		}
		
		
		if (f == 49425)
			f = f;
		
		//	InitializeBlackArray(black_count);
		black_count++;
		i = black_count-2;
		while (i >= 0 && black[i].frame > f) {
			black[i+1] = black[i];
			i--;
		}
		i++;
		
		black[i].frame = f;
		black[i].brightness = b;
		black[i].uniform = u;
		black[i].volume = v;
		black[i].cause = c;
	}
}



bool BuildMasterCommList(void) {
	int		i, j, t, c,b;
	int		a,k,count;
	int		cp, maxsc,rsc;
	int lastLogoTest;
	int 	silence_count = 0;
	int		silence_start = 0;
	int		summed_volume1 = 0;
	int		summed_volume2 = 0;
	int 	schange_found = false;
	int		schange_frame;
	int		low_volume_count;
	int		schange_max;
	int		mv,ms;
	int		volume_delta;
	int		p_vol, n_vol;
	int		plataus;

	double	length;
	long	totalFrames = 0;
	int		counter = 0;
	double	new_ar_ratio;
	FILE*	logo_file = NULL;
	bool	foundCommercials = false;
	time_t	ltime;

	if (frame_count == 0) {
		Debug(1, "No video found\n");
		return(false);
	}
	Debug(7, "Finished scanning file.  Starting to build Commercial List.\n");


	for (i = 1; i < 255; i++) {
		if (volumeHistogram[i] > 10) {
			min_volume = (i-1)*volumeScale;
			break;
		}
	}

	for (k = 1; k < 255; k++) {
		if (uniformHistogram[k] > 10) {
			min_uniform = (k-1)*UNIFORMSCALE;
			break;
		}
	}

	for (i = 0; i < 255; i++) {
		if (brightHistogram[i] > 1) {
			min_brightness_found = i;
			break;
		}
	}
	
	
	logoPercentage = (double) frames_with_logo / (double) framenum_real;

//	if (max_volume == 0) 
	{

#define VOLUME_DELTA	10
#define VOLUME_MAXIMUM	300
#define VOLUME_PLATAU_SIZE		6

		volume_delta = VOLUME_DELTA;

try_again:
		if (framearray) {			// Find silence volume level

			for (i = 0; i < 255; i++)
				silenceHistogram[i] = 0;
			plataus = 0;
			j = 1;
			for (i = VOLUME_PLATAU_SIZE; i < frame_count-VOLUME_PLATAU_SIZE;) {
				if (frame[i].volume > VOLUME_MAXIMUM || frame[i].volume < 0) {
					i++;
					continue;
				}
				while (i+1 < frame_count-VOLUME_PLATAU_SIZE && frame[i+1].volume < frame[i].volume)
					i++;
				k = 1;
				while (i-k - VOLUME_PLATAU_SIZE > 1 && 
					(abs(frame[i-k].volume - frame[i].volume) < volume_delta 
					//|| frame[i-k].volume < 50
					)) {
					k++;
				}
				if (frame[i-k].volume < frame[i].volume) {
					i++;
					continue;				
				}
				a = 1;
				while (i+a +VOLUME_PLATAU_SIZE < frame_count && 
					(abs(frame[i+a].volume - frame[i].volume) < volume_delta 
					//|| frame[i+a].volume < 50 
					)) {
					a++;
				}
				if (frame[i+a].volume < frame[i].volume) {
					i = i+a;
					continue;				
				}
 // i=8 k=1 a=11
				if (a+k > VOLUME_PLATAU_SIZE && i-k-VOLUME_PLATAU_SIZE > 0 && i+a+VOLUME_PLATAU_SIZE < frame_count) {
					p_vol = (frame[i-k-4].volume +
						frame[i-k-VOLUME_PLATAU_SIZE+3].volume + 
						frame[i-k-VOLUME_PLATAU_SIZE+2].volume + 
						frame[i-k-VOLUME_PLATAU_SIZE+1].volume + 
						frame[i-k-VOLUME_PLATAU_SIZE].volume) / 5;
					n_vol = (frame[i+a+4].volume +
						frame[i+a+VOLUME_PLATAU_SIZE-3].volume + 
						frame[i+a+VOLUME_PLATAU_SIZE-2].volume + 
						frame[i+a+VOLUME_PLATAU_SIZE-1].volume + 
						frame[i+a+VOLUME_PLATAU_SIZE].volume) / 5;
					if ( p_vol > frame[i].volume + 220 || n_vol > frame[i].volume + 220 )
						//if ( abs(frame[i-k-2].volume - frame[i].volume) > VOLUME_DELTA*2 ||					
						//	abs(frame[i+a+2].volume - frame[i].volume) > VOLUME_DELTA*2) 
					{
						Debug(8, "Platau@[%d] frames %d, volume %d, distance %d seconds\n", i, k+a, frame[i].volume, (int)((i-j)/fps));
						j = i;
//						for (j = i-k; j < i + a; j++)
//							frame[j].isblack |= C_v;

						plataus++;
						silenceHistogram[frame[i].volume/10]++;
					}
				}
				i += a;
			}
			a = 0;
			Debug(9, "Vol: Frames\n", i*10, silenceHistogram[i]);
			for (i = 0; i < 255; i++) {
				a += silenceHistogram[i];
				if (silenceHistogram[i] > 0)
					Debug(9, "%3d: %d\n", i*10, silenceHistogram[i]);

			}
			a = a * 6 / 10;
			j = 0;
			i = 0;
			while (j < a) {
				j += silenceHistogram[i++];
			}
			ms = i*10;
			Debug(7, "Calculated silence level = %d\n", ms);
			if (ms > 0 && ms < 30)
				ms = 30;
			if (ms < 50)
				mv = 2 * ms;
			else
			if (ms < 100)
				mv = 2 * ms;
			else
			if (ms < 200)
				mv = 2 * ms;
			else
				mv = 2 * ms;
		}
		if (mv == 0 || plataus < 5) {
			volume_delta *= 2;
			if (volume_delta < VOLUME_MAXIMUM)
				goto try_again;
		}
	}
	if (max_volume == 0) {
		max_volume = mv;
		max_silence = ms;
	}

	if (max_silence < min_volume + 30)
		max_silence = min_volume + 30;

	if (max_volume < 100) {
		if ( max_volume < min_volume + 30)
			max_volume = min_volume + 30;
	}
	else
	if (max_volume < min_volume + 100)
		max_volume = min_volume + 100;
	
	if (max_volume == 0) {

		if (framearray) {			// Find silence volume level

#define START_VOLUME	500
			count = 21;
scanagain:
			a = START_VOLUME;
			k = 0;
			if (frame[i].volume > 0)
				j = frame[i].volume;
			for (i = 1; i < frame_count; i++ ) {
				if (frame[i].volume > 0 && frame[i].volume < a) {
					if (frame[i].volume < j)
						j = frame[i].volume;
					k++;
					if (k > count && a > frame[i-count].volume + 20 &&
						j > a - 250) {
						i = i - count;
						a = frame[i].volume;
						j = frame[i].volume;
						k = 0;
					}
				}
				else
				{
					k = 0;
					if (frame[i].volume > 0)
						j = frame[i].volume;
				}
			}
		}
		if (a > START_VOLUME-100 && count > 7)
		{
			count = count - 7;
			goto scanagain;
		}
		max_silence = a+10;
		max_volume = a+150;
	}

	if (max_volume == 0) {

		for (k = 2; k < 255; k++) {
			if (volumeHistogram[k] > 10) {
				max_volume = k*volumeScale + 200;
				max_silence = k*volumeScale + 20;
				break;
			}

		}
/*

		max_volume = 1000;
		for (k = black_count - 1; k >= 0; k--) {
			if (black[k].volume >= 0 && black[k].volume <  max_volume)
				max_volume = black[k].volume;	
		}
		max_volume *= 4;
 */
		Debug ( 1, "Setting max_volume to %i\n", max_volume);
	}

	if (commDetectMethod & LOGO) {
		// close out last logo cblock if one is open
		ProcessLogoTest(frame_count, false, true);
/*		
		if (loadingCSV) {
			prev_logo_threshold = logo_threshold-1.0;
			FindLogoThreshold();
			if (fabs(logo_threshold - prev_logo_threshold) > 0.4) {
				Debug(2,"Changed logo_threshold to %.2f, recalculating logo timeline\n", logo_threshold);
				InitProcessLogoTest();
				for (i = 1; i < frame_count; i++) {
					curLogoTest = (frame[i].currentGoodEdge > logo_threshold);
					lastLogoTest = ProcessLogoTest(i, curLogoTest);
					frame[i].logo_present = lastLogoTest;
					if (lastLogoTest) frames_with_logo++;
				}
				logoPercentage = (double) frames_with_logo / (double) framenum_real;
			}
			else
				logo_threshold = prev_logo_threshold;
			
		}
*/

		if (logo_quality == 0.0)
			FindLogoThreshold();

		// Clean up logo blocks
/*
		for (i = logo_block_count-2; i >= 0; i--) {
			if (logo_block[i+1].start - logo_block[i].end < min_commercial_size * fps + (int)(2*shrink_logo*fps)) {
				Debug(1, "Logo cblock %d and %d combined because gap (%i s) too short with previous\n", i, i+1, (int)((logo_block[i+1].start - logo_block[i].end )/ fps));
				logo_block[i+1].start = logo_block[i].start;
				for (t = i; t+1 < logo_block_count; t++) {
					logo_block[t] = logo_block[t+1];
				}
				logo_block_count--;
			}
		}
*/
		for (i = logo_block_count-1; i >= 0; i--) {
			if (logo_block[i].end - logo_block[i].start < min_commercial_size * fps - (int)(2*shrink_logo*fps)) {
				Debug(1, "Logo cblock %d deleted because too short (%i s)\n", i, (int)((logo_block[i].end - logo_block[i].start)/fps) );
				for (t = i; t+1 < logo_block_count; t++) {
					logo_block[t] = logo_block[t+1];
				}
				logo_block_count--;
			}
		}
		if (logoPercentage > logo_fraction && after_logo > 0 && framearray && logo_block_count > 0) {
			for (i = 0; i < logo_block_count; i++) {
				if (i < logo_block_count-1 && logo_block[i+1].start - logo_block[i].end < max_commercialbreak * fps/4)
					continue;			// Don't do anything if too close
				if (i == logo_block_count-1 && frame_count - logo_block[i].end < max_commercialbreak * fps/4)
					continue;			// Don't do anything if too close
				if (after_logo==999) {
					j = logo_block[i].end;
					InsertBlackFrame(j,frame[j].brightness,frame[j].uniform,frame[j].volume, C_t);
					Debug(
						3,
						"Frame %6i - Cutpoint added when Logo disappears\n",
						j
					);
					continue;
				}

				j = logo_block[i].end + (int)(after_logo * fps);
				if ( j > frame_count)
					j = frame_count-1;
				t = j + (int)(30 * fps);
				if ( t > frame_count)
					t = frame_count-1;
				maxsc = 255;
				cp = 0;
				while (j < t) {
					rsc = 255;
					while (frame[j].volume >= max_volume && j < t) {
//						if (rsc > frame[j].schange_percent)
//							rsc = frame[j].schange_percent;
						j++;
					}
					while (frame[j].volume < max_volume && j < t) {
						if (rsc > frame[j].schange_percent)
							rsc = frame[j].schange_percent;
						j++;
					}
					c = 10;
					while (c-- && j < t) {
						if (rsc > frame[j].schange_percent)
							rsc = frame[j].schange_percent;
						j++;
					}
						if (maxsc > rsc) {
							maxsc = rsc;
							cp = j;
						}
//					cp = j;
//					j=t;
				}
				if (cp != 0) {
					InsertBlackFrame(cp,frame[cp].brightness,frame[cp].uniform,frame[cp].volume, C_t);
					Debug(
						3,
						"Frame %6i - Cutpoint added %i seconds after Logo disappears at change percentage of %d\n",
						cp, (int)((cp - logo_block[i].end)/fps), maxsc
					);
				}
			}
		}

		if (logoPercentage > logo_fraction && before_logo > 0 && framearray && logo_block_count > 0) {
			for (i = 0; i < logo_block_count; i++) {
				if (i > 0 && logo_block[i].start - logo_block[i-1].end < max_commercialbreak * fps/4)
					continue;
				if (i == 0 && logo_block[i].start < max_commercialbreak * fps/4)
					continue;
				if (before_logo==999) {
					j = logo_block[i].start;
					InsertBlackFrame(j,frame[j].brightness,frame[j].uniform,frame[j].volume, C_t);
					Debug(
						3,
						"Frame %6i - Cutpoint added when Logo appears\n",
						j
					);

					continue;
				}

				j = logo_block[i].start - (int)(before_logo * fps);
				if ( j < 1)
					j = 1;

				b = black_count-1;
				while (b >= 0 && black[b].frame > j) {
					b--;
				}
				t = j - (int)(30 * fps);
				if (b >= 0 && t < black[b].frame + 5*(int)(fps))
					t = black[b].frame + 5*(int)(fps);

				if ( t < 1)
					t = 1;
				while (frame[j].volume > max_volume && j > t)
					j--;
				while (frame[j].volume < max_volume && j > t)
					j--;
				if (j > t) {
					InsertBlackFrame(j,frame[j].brightness,frame[j].uniform,frame[j].volume, C_l);
					Debug(
						3,
						"Frame %6i - Cutpoint added %i seconds before Logo appears\n",
						j, (int)((logo_block[i].start - j)/fps)
					);
				}
			}
		}
//		if (logoPercentage > .15 && logoPercentage < .32 ) {
//			reverseLogoLogic = true;
//			logoPercentage = 1 - logoPercentage;
//		}
		if (logoPercentage < logo_fraction - 0.05 || logoPercentage > logo_percentile) {
			Debug(1, "\nNot enough or too much logo's found (%.2f), disabling the use of Logo detection\n",logoPercentage );
			commDetectMethod -= LOGO;
		}
	}

	if (commDetectMethod & SILENCE) {
		silence_count = 0;
		schange_found = false;
		schange_frame = 0;
		schange_max = 100;
		low_volume_count = 0;
		for (i=1; i <frame_count; i++) { 
			if (i == 21361)
				i = i;
			if (min_silence > 0) {
				if (0 <= frame[i].volume && (frame[i].volume < 50 || frame[i].volume < max_silence)) {
					if (silence_start == 0)
						silence_start = i;
					silence_count++;
					if (frame[i].schange_percent < schange_threshold) {
						schange_found = true;
						if (schange_max > frame[i].schange_percent) {
							schange_frame = i;
							schange_max = frame[i].schange_percent;
						}
					}
					if (frame[i].uniform < non_uniformity) {
						schange_found = true;
						schange_frame = i;
					}
					if (frame[i].volume < 15) {
						low_volume_count++;
					}
				} else {
					if (silence_count > min_silence /* * (int)fps */ && silence_count < 5 * fps) {

					if ( /*low_volume_count > (int)(silence_count * 0.7) || */ schange_found || frame[i].schange_percent < schange_threshold) {
#define SILENCE_CHECK	((int)(2.5 * fps))
						summed_volume1 = 0;
						for (j = max(silence_start - SILENCE_CHECK,1); j < silence_start; j++) {
//							if (summed_volume1 < frame[j].volume)
								summed_volume1 += frame[j].volume;
						}
						summed_volume1 /= min(SILENCE_CHECK, silence_start+1) ;
						summed_volume2 = 0;
						for (j = i; j < min(i+SILENCE_CHECK, frame_count); j++) {
//							if (summed_volume2 < frame[j].volume)
								summed_volume2 += frame[j].volume;
						}
						summed_volume2 /= min(SILENCE_CHECK, frame_count - i + 1);
						if ((summed_volume1 > 0.9*max_volume &&  summed_volume2 > 0.9*max_volume && low_volume_count > min_silence ) ||
							(summed_volume1 > 2*max_volume &&  summed_volume2 > 2*max_volume) ||
							(summed_volume1 > 4*max_volume ||  summed_volume2 > 4*max_volume)
							) {
							if (schange_frame == 0)
								schange_frame = i;

#if 1							
							for (j=silence_start; j < i; j++) {
								frame[j].isblack |= C_v;
								InsertBlackFrame(j,frame[j].brightness,frame[j].uniform,frame[j].volume, C_v);
							}
#else
							frame[schange_frame].isblack |= C_v;
							InsertBlackFrame(schange_frame,frame[schange_frame].brightness,frame[schange_frame].uniform,frame[schange_frame].volume, C_v);
#endif
							//for (j = silence_start /*i - min_silence /* * (int)fps */; j <= i; j++) {
							//	frame[j].isblack |= C_v;
							//	InsertBlackFrame(j,frame[j].brightness,frame[j].uniform,frame[j].volume, C_v);
							//}
						}
					}}
					silence_start = 0;					
					silence_count = 0;
					schange_found = false;
					schange_frame = 0;
					schange_max = 100;
					low_volume_count = 0;
				}
			}
		}
	}


	after_start = added_recording * fps * 60;
	before_end  = frame_count - added_recording * fps * 60;


	// close out the last ar cblock
	if (commDetectMethod & AR) {
		if (ar_block[ar_block_count].start > 0) {
			Debug(5, "The last ar cblock wasn't closed.  Now closing.\n");
			ar_block[ar_block_count].end = frame_count;
			ar_block_count++;
		}


		// Print out ar cblock list
		Debug(9, "\nPrinting AR cblock list before cleaning\n-----------------------------------------\n");
		for (i = 0; i < ar_block_count; i++) {
			Debug(
				9,
				"Block: %i\tStart: %6i\tEnd: %6i\tAR_R: %.2f\tLength: %s, [%4dx%4d] minX=%3d, minY=%3d, maxX=%3d, maxY=%3d\n",
				i,
				ar_block[i].start,
				ar_block[i].end,
				ar_block[i].ar_ratio,
				dblSecondsToStrMinutes((ar_block[i].end - ar_block[i].start) / fps),
				ar_block[i].width, ar_block[i].height,
				ar_block[i].minX, ar_block[i].minY, ar_block[i].maxX, ar_block[i].maxY
			);
		}

		// Calculate histogram with noisy aspect ratios
		FillARHistogram(false);

		// Update histogram to remove replaced ratios
		for (i = 0 ; i < MAX_ASPECT_RATIOS; i++) {
			for (j = i+1; j < MAX_ASPECT_RATIOS; j++) {
				if (ar_histogram[j].ar_ratio < ar_histogram[i].ar_ratio+ar_delta && 
					ar_histogram[j].ar_ratio > ar_histogram[i].ar_ratio-ar_delta )
					ar_histogram[j].ar_ratio = ar_histogram[i].ar_ratio;
			}
		
		}		
		
		// Normalize aspect ratios
		for (i = 0; i < ar_block_count; i++) {
			new_ar_ratio = FindARFromHistogram (ar_block[i].ar_ratio);
			ar_block[i].ar_ratio = new_ar_ratio; 
		}
		// Calculate histogram with normalized aspect ratios
		FillARHistogram(true);
		dominant_ar = ar_histogram[0].ar_ratio;


again:
		// Clean up ar cblock list

		for (i = ar_block_count - 1; i > 0; i--) {
			length = ar_block[i].end - ar_block[i].start;

			if (cut_on_ar_change > 2 && length < cut_on_ar_change*(int)fps && ar_block[i].ar_ratio != AR_UNDEF ) {
				Debug(
					6,
					"Undefining AR cblock %i because it is too short\n",
					i,
					dblSecondsToStrMinutes(length / fps)
				);
				ar_block[i].ar_ratio = AR_UNDEF;
				goto again;
			}

/*
			if (ar_block[i].ar_ratio == AR_UNDEF && length < 5*(int)fps) {
				ar_block[i - 1].end = ar_block[i].end;
				ar_block_count--;
				Debug(
					6,
					"Deleting AR cblock %i because it is too short\n",
					i,
					dblSecondsToStrMinutes(length / fps)
				);
				for (j = i; j < ar_block_count; j++) {
					ar_block[j].start = ar_block[j + 1].start;
					ar_block[j].end = ar_block[j + 1].end;
					ar_block[j].ar_ratio = ar_block[j + 1].ar_ratio;
				}
				goto again;
			}
*/
#if 1
			if (commDetectMethod & LOGO && 	ar_block[i - 1].ar_ratio != AR_UNDEF &&	
				ar_block[i].ar_ratio > ar_block[i - 1].ar_ratio &&
				CheckFrameForLogo(ar_block[i-1].end) &&
				CheckFrameForLogo(ar_block[i].start) ) {
				if (ar_block[i].end - ar_block[i].start > ar_block[i-1].end - ar_block[i-1].start) {
					j = ar_block[i-1].start;
					ar_block[i-1] = ar_block[i];
					ar_block[i-1].start = j;
				} else
					ar_block[i - 1].end = ar_block[i].end;
				ar_block_count--;
				Debug(
					6,
					"Joining AR blocks %i and %i because both have logo\n",
					i - 1,
					i,
					i,
					dblSecondsToStrMinutes(length / fps)
				);
				for (j = i; j < ar_block_count; j++) {
					ar_block[j] = ar_block[j + 1];
				}
				goto again;
			}
//
#endif
			if ( i == 1 && ar_block[i-1].ar_ratio == AR_UNDEF) {
				j = ar_block[i - 1].start;
				ar_block[i - 1] = ar_block[i];
				ar_block[i - 1].start = j;
				ar_block_count--;
				Debug(6, "Joining AR blocks %i and %i because cblock 0 has an AR ratio of 0.0\n", i - 1, i, ar_block[i-1].ar_ratio);
				for (j = i; j < ar_block_count; j++) {
					ar_block[j] = ar_block[j + 1];
				}
				goto again;

			}
			if (( ar_block[i].ar_ratio - ar_block[i - 1].ar_ratio < ar_delta &&
				  ar_block[i].ar_ratio - ar_block[i - 1].ar_ratio > -ar_delta )) {
				ar_block[i - 1].end = ar_block[i].end;
				ar_block_count--;
				Debug(6, "Joining AR blocks %i and %i because both have an AR ratio of %.2f\n", i - 1, i, ar_block[i].ar_ratio);
				for (j = i; j < ar_block_count; j++) {
					ar_block[j] = ar_block[j + 1];
				}
				goto again;

			}
			if (  ar_block[i-1].ar_ratio == AR_UNDEF && i > 1 &&
				  ar_block[i].ar_ratio - ar_block[i - 2].ar_ratio < ar_delta &&
				  ar_block[i].ar_ratio - ar_block[i - 2].ar_ratio > -ar_delta ) {
				ar_block[i - 2].end = ar_block[i].end;
				ar_block_count -= 2;
				Debug(6, "Joining AR blocks %i and %i because they have a dummy cblock inbetween\n", i - 2, i);
				for (j = i-1; j < ar_block_count; j++) {
					ar_block[j] = ar_block[j + 2];
				}
				goto again;

			}
		}

		// Print out ar cblock list
		Debug(4, "\nPrinting AR cblock list\n-----------------------------------------\n");
		for (i = 0; i < ar_block_count; i++) {
			Debug(
				4,
				"Block: %i\tStart: %6i\tEnd: %6i\tAR_R: %.2f\tLength: %s, [%4dx%4d] minX=%3d, minY=%3d, maxX=%3d, maxY=%3d\n",
				i,
				ar_block[i].start,
				ar_block[i].end,
				ar_block[i].ar_ratio,
				dblSecondsToStrMinutes((ar_block[i].end - ar_block[i].start) / fps),
				ar_block[i].width, ar_block[i].height,
				ar_block[i].minX, ar_block[i].minY, ar_block[i].maxX, ar_block[i].maxY
			);
		}
	}

	// close out the last cc cblock
	if (processCC) {
		cc_block[cc_block_count].end_frame = frame_count;
		cc_block_count++;
		cc_text[cc_text_count].end_frame = frame_count;
		cc_text_count++;
		for (i = cc_text_count - 1; i > 0; i--) {
			if (cc_text[i].text_len == 0) {
				for (j = i; j < cc_text_count; j++) {
					cc_text[j].start_frame = cc_text[j + 1].start_frame;
					cc_text[j].end_frame = cc_text[j + 1].end_frame;
					cc_text[j].text_len = cc_text[j + 1].text_len;
          strcpy((char*)cc_text[j].text, (char*)cc_text[j + 1].text);
				}

				cc_text_count--;
			}
		}

		Debug(2, "Closed caption transcript\n--------------------\n");
		
		for (i = 0; i < cc_text_count; i++) {
			Debug(
				2,
				"%i) S:%6i E:%6i L:%4i %s\n",
				i,
				cc_text[i].start_frame,
				cc_text[i].end_frame,
				cc_text[i].text_len,
				cc_text[i].text
			);
		}
	}

	if (output_framearray) OutputFrameArray(false);

	BuildBlocks(false);
	if (commDetectMethod & LOGO) {
		PrintLogoFrameGroups();
	}
	WeighBlocks();
	
	foundCommercials = OutputBlocks();

	if (verbose) {
		Debug(1, "\n%i Frames Processed\n", framesprocessed);
		log_file = fopen(logfilename, "a+");
		fprintf(log_file, "################################################################\n");
		time(&ltime);
		fprintf(log_file, "Time at end of run:\n%s", ctime(&ltime));
		fprintf(log_file, "################################################################\n");
		fclose(log_file);
		log_file = NULL;
	}

	
	if (ccCheck && processCC) {
		char temp[MAX_PATH];
		FILE* tempFile;
		if ((most_cc_type == PAINTON) || (most_cc_type == ROLLUP) || (most_cc_type == POPON)) {
			sprintf(temp, "%s.ccyes", basename);
			tempFile = fopen(temp, "w");
			fclose(tempFile);
			sprintf(temp, "%s.ccno", basename);
			remove(temp);
		} else {
			sprintf(temp, "%s.ccno", basename);
			tempFile = fopen(temp, "w");
			fclose(tempFile);
			sprintf(temp, "%s.ccyes", basename);
			remove(temp);
		}
	}
	
	if (deleteLogoFile) {
		logo_file = fopen(logofilename, "r");
		if(logo_file) {
			fclose(logo_file);
			remove(logofilename);
		}
	}

//	free(frame);
	return (foundCommercials);
}

bool WithinDivisibleTolerance(double test_number, double divisor, double tolerance) {
	double	added;
	double	remainder;
	added = test_number + tolerance;
	remainder = added - divisor * ((int)(added / (double)divisor));
	return ((remainder >= 0) && (remainder <= (2 * tolerance)));
}

/*
void CalculateCorrelation()
{
	int i,j;
	double position;
	double length;
	double min_weight;
	double pivot;
	double correlation;
	double distance;
	double weight;

	for (i = 0; i < block_count; i++) {
		pivot = ((double)(cblock[i].f_start+cblock[i].f_end)/(2*frame_count));
		length = ((double)(cblock[i].f_end - cblock[i].f_start)/frame_count);
		correlation = 0;
		min_weight = length;
		for (j = 0; j < block_count; j++) {
			if (i != j) {
				distance = fabs(pivot - ((double)(cblock[j].f_start+cblock[j].f_end)/(2*frame_count)));
				weight = ((double)(cblock[j].f_end - cblock[j].f_start)/frame_count);
				if (min_weight > weight)
					min_weight = weight;
				correlation += (1 / (distance * distance) ) / (weight );
			}
		}
		cblock[i].correlation = correlation / length * (min_weight*min_weight*min_weight);
	}

}

void CalculateFit()
{
	int i,j;
	int start;
	double position;
	double length;
	double min_weight;
	double pivot;
	double correlation;
	double prev_correlation = 0;
	double distance;
	double weight;

	for (i = 0; i < block_count; i++) {
		start = cblock[i].f_start;
		correlation = 0;
		j = i+1;
		if (cblock[i].f_end - cblock[i].f_start < max_commercial_size * fps && j < block_count) {
			while ( cblock[j].f_end - cblock[j].f_start < max_commercial_size * fps && j < block_count - 1 && cblock[j].f_end < start + max_commercialbreak * fps)
				j++;
			if ( cblock[j].f_start > start + min_commercialbreak * fps) {
				correlation = j - i;
			}
		}
		if (correlation > prev_correlation)
			prev_correlation = correlation;
		cblock[i].correlation = prev_correlation;
		prev_correlation -= 1;
	}
}

*/

// Match string ([*+]*[CS]+)*M([*+]*[CS]+)*

int beforeblocks[100];
int afterblocks[100];
/*
int MatchBlocks(int k, char *t)
{
	int match = false;
	char after[80];
	int i;
	int j;

	i = 0;
	while (t[i] != 0 && t[i] != 'M')
		i++;
	if (t[i] == 0) 
		return(0);
	j = 0;
	while (t[i+j+1] != 0)
		after[j] = t[i+j+1];
		j++;
}
*/

int length_order[2000];
int min_val[10];
int max_val[10];
int delta_val[10];


void BuildPunish()
{
	int i;
	int j;
	int t;
	int l;
	int shorttest;
	for (i=0 ; i< block_count; i++)
		length_order [i] = i;
again:
	for (j=0; j < block_count; j++) {
		for (i=j ; i< block_count; i++) {
			if (cblock[length_order[i]].length > cblock[length_order[j]].length){
				t = length_order[j];
				length_order[j] = length_order[i];
				length_order[i] = t;
				goto again;
			}	
		}
	}
	max_val[0] = min_val[0] = cblock[length_order[0]].brightness;
	max_val[1] = min_val[1] = cblock[length_order[0]].volume;
	max_val[2] = min_val[2] = cblock[length_order[0]].silence;
	max_val[3] = min_val[3] = cblock[length_order[0]].uniform;
	max_val[4] = min_val[4] = cblock[length_order[0]].ar_ratio;
	max_val[5] = min_val[5] = cblock[length_order[0]].schange_rate;
	l = 0;
	for (i = 0; i < block_count; i++) {
		l += cblock[length_order[i]].length * fps;
#define MINMAX(I,FIELD)	{	if (min_val[I] > cblock[length_order[i]].FIELD)			min_val[I] = cblock[length_order[i]].FIELD; 		if (max_val[I] < cblock[length_order[i]].FIELD) 			max_val[I] = cblock[length_order[i]].FIELD; }
		MINMAX(0, brightness)
		MINMAX(1, volume)
		MINMAX(2, silence)
		MINMAX(3, uniform)
		MINMAX(4, ar_ratio)
		MINMAX(5, schange_rate)
		if (l > cblock[block_count - 1].f_end* 70 / 100)
			break;
	}

}

void WeighBlocks(void) {
	int		i;
	int		j;
	int		k;
	double	combined_length;
	double	tolerance;
	double	sumb = 0.0;
	double	sumf = 0.0;
	double  wscore = 0.0;
	double  lscore = 0.0;
	bool	end_deleted = false;
	bool	start_deleted = false;
	int		framecount = 0;
	double	max_score = 99.99;
	int		max_combined_count = 25;
	bool	breakforcombine = false;
	double	schange_modifier;

	if (commDetectMethod & AR) {
//		showAvgAR = AverageARForBlock(1, framesprocessed);
		SetARofBlocks();
	}


	for (i = 0; i < block_count-2; i++) {
		if (CUTCAUSE(cblock[i].cause) == C_a  && CUTCAUSE(cblock[i+1].cause) == C_a  &&
			cblock[i+1].length < 3.0 &&
			fabs(cblock[i].ar_ratio - cblock[i+2].ar_ratio) < ar_delta
			) {
			Debug(2, "Deleting cblock %d starting at frame %d because too short and same AR before and after\n", i+1, cblock[i+1].f_start);
			cblock[i].b_tail = cblock[i+2].b_tail;
			cblock[i].f_end = cblock[i+2].f_end;
			cblock[i].length += cblock[i+1].length + cblock[i+2].length;
			for (j = i+1; j < block_count-2; j++) {
				cblock[j] = cblock[j+2];
			}
			block_count = block_count - 2;
		}
	}



	if (processCC) {
		PrintCCBlocks();
		for (i = 0; i < block_count; i++) {
			cblock[i].cc_type = DetermineCCTypeForBlock(cblock[i].f_start, cblock[i].f_end);
		}
	}


	if (commDetectMethod & LOGO) {
		if (logoPercentage < logo_fraction - 0.05 || logoPercentage > logo_percentile) {
			Debug(1, "Not enough or too much logo's found, disabling the use of Logo detection\n", i);
			commDetectMethod -= LOGO;
			max_score = 10000;
		}
	}
	for (i = 0; i < block_count; i++) {
		if (commDetectMethod & LOGO) {
			cblock[i].logo = CalculateLogoFraction(cblock[i].f_start, cblock[i].f_end);
		} else
			cblock[i].logo = 0;
	}

//	CalculateCorrelation();
//	CalculateFit();

    CleanLogoBlocks();		// Can join blocks, so recalculate logo

	if (commDetectMethod & SCENE_CHANGE) {
		for (i = 0; i < block_count; i++) {
			Debug(5, "Block %.3i\tschange_rate - %.2f\t average - %.2f\n", i, cblock[i].schange_rate, avg_schange);
		}
	}

	for (i = 0; i < block_count; i++) {
		if (commDetectMethod & LOGO) {
			cblock[i].logo = CalculateLogoFraction(cblock[i].f_start, cblock[i].f_end);
		} else
			cblock[i].logo = 0;
	}


	if ((commDetectMethod & LOGO) && logoPercentage > 0.4) {
		if (score_percentile + logoPercentage < 1.0)
			score_percentile = logoPercentage + score_percentile;
	} else if (score_percentile < 0.5) 
		score_percentile = 0.71;

//	if ((commDetectMethod & LOGO) && logoPercentage > logo_fraction && logoPercentage < logo_percentile && logo_present_modifier != 1.0)
//		excessive_length_modifier = 1;		// TESTING!!!!!!!!!!!!!!!!!! 

	Debug(5, "\nFuzzy scoring of the blocks\n---------------------------\n");



	for (i = 0; i < block_count; i++) {
		if (i == 0 || true /*(cblock[i-1].cause & (C_b | C_u | C_v)) || cut_on_ar_change == 2 || 	(!(commDetectMethod & BLACK_FRAME) && (cblock[j].cause & C_v))  */) {
			j = i;
			combined_length = cblock[i].length;
//			while (j < block_count && ((cblock[j].cause & C_a) && (cut_on_ar_change == 1)  && !	(!(commDetectMethod & BLACK_FRAME) && (cblock[j].cause & C_v))  ) ) {
//				j++;
//				combined_length += cblock[j].length;
//			}
expand:
			k = j;
			if (i > 0 && ((CUTCAUSE(cblock[i-1].cause) == C_b) || (CUTCAUSE(cblock[i-1].cause) == C_u)))
				combined_length -= cblock[i].b_head / fps / 4 ;
				
			if ((CUTCAUSE(cblock[i].cause) == C_b) || (CUTCAUSE(cblock[i].cause) == C_u))
				combined_length -= cblock[j+1].b_head / fps / 4 ;

			combined_length -= (cblock[i].b_head + cblock[j + 1].b_head) / fps / 4;
			tolerance = (cblock[i].b_head + cblock[j + 1].b_head + 4) / fps;
			if (IsStandardCommercialLength(combined_length, tolerance, true)) {
				while (j>=i) {
					cblock[j].strict = 2;
					Debug(2, "Block %i has strict standard length for a commercial.\n", j);
					Debug(3, "Block %i score:\tBefore - %.2f\t", j, cblock[j].score);
					cblock[j].score *= length_strict_modifier;
//					cblock[j].score *= length_strict_modifier;
					Debug(3, "After - %.2f\n", cblock[j].score);
					cblock[j].cause |= C_STRICT;
					cblock[j].more |= C_STRICT;
					j--;
				}
			} else if (IsStandardCommercialLength(combined_length, tolerance, false)) {
				while (j>=i) {
					cblock[j].strict = 1;
					Debug(2, "Block %i has non-strict standard length for a commercial.\n", j);
					Debug(3, "Block %i score:\tBefore - %.2f\t", j, cblock[j].score);
					cblock[j].score *= length_nonstrict_modifier;
					cblock[j].score = (cblock[j].score > max_score) ? max_score : cblock[j].score;
					Debug(3, "After - %.2f\n", cblock[j].score);
					cblock[j].cause |= C_NONSTRICT;
					cblock[j].more |= C_NONSTRICT;
					j--;
				}
			} else {
				while (j>=i) {
					cblock[j].strict = 0;
					j--;
				}
			}
			j = k;
//			if (j+1 < block_count && cblock[i].strict == 0 && cblock[j+1].length < 5.0) {
//				j++;
//				combined_length += cblock[j].length;
//				goto expand;
//			}
		}
/*
		tolerance = (cblock[i].bframe_count + cblock[i + 1].bframe_count + 6) / fps;
		if (IsStandardCommercialLength(cblock[i].length, tolerance, true)) {
			cblock[i].strict = 2;
			Debug(2, "Block %i has strict standard length for a commercial.\n", i);
			Debug(3, "Block %i score:\tBefore - %.2f\t", i, cblock[i].score);
			cblock[i].score *= length_strict_modifier;
			cblock[i].score *= length_strict_modifier;
			Debug(3, "After - %.2f\n", cblock[i].score);
			cblock[i].cause |= C_STRICT;
		} else if (IsStandardCommercialLength(cblock[i].length, tolerance, false)) {
			cblock[i].strict = 1;
			Debug(2, "Block %i has non-strict standard length for a commercial.\n", i);
			Debug(3, "Block %i score:\tBefore - %.2f\t", i, cblock[i].score);
			cblock[i].score *= length_nonstrict_modifier;
			cblock[i].score = (cblock[i].score > max_score) ? max_score : cblock[i].score;
			Debug(3, "After - %.2f\n", cblock[i].score);
			cblock[i].cause |= C_NONSTRICT;
		} else
			cblock[i].strict = 0;
*/

#if 1
		if (cblock[i].combined_count < max_combined_count) {
//			Debug(3, "Attempting to combine cblock %i\n", i);
			combined_length = cblock[i].length;
			for (j = 1; j < block_count - i; j++) {
				if (IsStandardCommercialLength(cblock[i + j].length - (cblock[i+j].b_head + cblock[i + j + 1].b_head) / fps, 
											   (cblock[i+j].bframe_count + cblock[i + j + 1].bframe_count + 2) / fps, true)) {
/*					Debug(
						3,
						"Not attempting to forward combine blocks %i to %i because cblock %i is strict commercial.\n",
						i,
						i + j,
						i + j
					);
*/
					break;
				}
				if ((cblock[i + j].combined_count > max_combined_count) || (cblock[i].combined_count > max_combined_count)) {
					Debug(
						3,
						"Not attempting to forward combine blocks %i to %i because cblock %i has already been combined %i times.\n",
						i,
						i + j,
						i + j,
						cblock[i + j].combined_count
					);
					breakforcombine = true;
					break;
				}

				tolerance = (cblock[i].bframe_count + cblock[i + j + 1].bframe_count + 2) / fps;
				combined_length += cblock[i + j].length;
				if (combined_length > (max_commercial_size) + tolerance) {
//					Debug(2, "Not trying to combine blocks %i thru %i due to excessive length - %f\n", i, i + j, combined_length);
					break;
				} else {
					if (IsStandardCommercialLength(combined_length - (cblock[i].b_head + cblock[i + j + 1].b_head) / fps, tolerance, true) && combined_length_strict_modifier != 1.0) {
						Debug(
							2,
							"Combining Blocks %i thru %i result in strict standard commercial length of %.2f with a tolerance of %f.\n",
							i,
							i + j,
							combined_length,
							tolerance
						);
						for (k = 0; k <= j; k++) {
							Debug(3, "Block %i score:\tBefore - %.2f\t", i + k, cblock[i + k].score);
							cblock[i + k].score *= 1 + (combined_length_strict_modifier / (j + 1) / 2);
							cblock[i + k].score = (cblock[i + k].score > max_score) ? max_score : cblock[i + k].score;
							cblock[i + k].combined_count += 1;
							Debug(3, "After - %.2f\tCombined count - %i\n", cblock[i + k].score, cblock[i + k].combined_count);
							cblock[i + k].cause |= C_COMBINED;
							cblock[i + k].more |= C_COMBINED;

						}
					} else if (IsStandardCommercialLength(combined_length - (cblock[i].b_head + cblock[i + j + 1].b_head) / fps, tolerance, false) && combined_length_nonstrict_modifier != 1.0) {
						Debug(
							2,
							"Combining Blocks %i thru %i result in non-strict standard commercial length of %.2f with a tolerance of %f.\n",
							i,
							i + j,
							combined_length,
							tolerance
						);
						for (k = 0; k <= j; k++) {
							Debug(3, "Block %i score:\tBefore - %.2f\t", i + k, cblock[i + k].score);
							cblock[i + k].score *= 1 + (combined_length_nonstrict_modifier / (j + 1) / 2);
							cblock[i + k].score = (cblock[i + k].score > max_score) ? max_score : cblock[i + k].score;
							cblock[i + k].combined_count += 1;
							Debug(3, "After - %.2f\tCombined count - %i\n", cblock[i + k].score, cblock[i + k].combined_count);
							cblock[i + k].cause |= C_COMBINED;
							cblock[i + k].more |= C_COMBINED;
						}
					}
				}
			}

			if (breakforcombine) {
//				Debug(3, "Block %i Break for forward combined limit\n", i);
				breakforcombine = false;
			}

			combined_length = cblock[i].length;
			for (j = 1; j < i; j++) {
				if (IsStandardCommercialLength(cblock[i - j].length - (cblock[i-j].b_head + cblock[i - j + 1].b_head) / fps, (cblock[i-j].bframe_count + cblock[i - j + 1].bframe_count + 2) / fps, true)) {
/*					Debug(
						3,
						"Not attempting to forward combine blocks %i to %i because cblock %i is strict commercial.\n",
						i - j,
						i,
						i - j
					);
*/
					break;
				}
				if ((cblock[i - j].combined_count > max_combined_count) || (cblock[i].combined_count > max_combined_count)) {
					Debug(
						3,
						"Not attempting to backward combine blocks %i to %i because cblock %i has already been combined %i times.\n",
						i - j,
						i,
						i - j,
						cblock[i - j].combined_count
					);
					breakforcombine = true;
					break;
				}

				tolerance = (cblock[i + 1].bframe_count + cblock[i - j].bframe_count + 2) / fps;
				combined_length += cblock[i - j].length;
				if (combined_length >= max_commercial_size) {
//					Debug(2, "Not trying to backward combine blocks %i thru %i due to excessive length - %f\n", i - j, i, combined_length);
					break;
				} else {
					if (IsStandardCommercialLength(combined_length - (cblock[i + 1].b_head + cblock[i - j].b_head) / fps, tolerance, true) && combined_length_strict_modifier != 1.0) {
						Debug(
							2,
							"Combining Blocks %i thru %i result in strict standard commercial length of %.2f with a tolerance of %f.\n",
							i - j,
							i,
							combined_length,
							tolerance
						);
						for (k = 0; k <= j; k++) {
							Debug(3, "Block %i score:\tBefore - %.2f\t", i - k, cblock[i - k].score);
							cblock[i - k].score *= 1 + (combined_length_strict_modifier / (j + 1) / 2);
							cblock[i - k].score = (cblock[i - k].score > max_score) ? max_score : cblock[i - k].score;
							cblock[i - k].combined_count += 1;
							Debug(3, "After - %.2f\tCombined count - %i\n", cblock[i - k].score, cblock[i - k].combined_count);
							cblock[i - k].cause |= C_COMBINED;
							cblock[i - k].more |= C_COMBINED;
						}
					} else if (IsStandardCommercialLength(combined_length - (cblock[i + 1].b_head + cblock[i - j].b_head) / fps, tolerance, false) && combined_length_nonstrict_modifier != 1.0) {
						Debug(
							2,
							"Combining Blocks %i thru %i result in non-strict standard commercial length of %.2f with a tolerance of %f.\n",
							i - j,
							i,
							combined_length,
							tolerance
						);
						for (k = 0; k <= j; k++) {
							Debug(3, "Block %i score:\tBefore - %.2f\t", i - k, cblock[i - k].score);
							cblock[i - k].score *= 1 + (combined_length_nonstrict_modifier / (j + 1) / 2);
							cblock[i - k].score = (cblock[i - k].score > max_score) ? max_score : cblock[i - k].score;
							cblock[i - k].combined_count += 1;
							Debug(3, "After - %.2f\tCombined count - %i\n", cblock[i - k].score, cblock[i - k].combined_count);
							cblock[i - k].cause |= C_COMBINED;
							cblock[i - k].more |= C_COMBINED;

						}
					}
				}
			}

			if (breakforcombine) {
//				Debug(3, "Block %i Break for backward combined limit\n", i);
				breakforcombine = false;
			}
		}
#endif
		// if logo detected in cblock, score = 10%
		if (commDetectMethod & LOGO) {
			if (cblock[i].logo > 0.20) {
				Debug(2, "Block %i has logo.\n", i);
				Debug(3, "Block %i score:\tBefore - %.2f\t", i, cblock[i].score);
				cblock[i].score *= logo_present_modifier;
//				cblock[i].score *= (logo_present_modifier*cblock[i].logo) + (1-cblock[i].logo);
				cblock[i].score = (cblock[i].score > max_score) ? max_score : cblock[i].score;
				Debug(3, "After - %.2f\n", cblock[i].score);
				cblock[i].cause |= C_LOGO;
				cblock[i].less |= C_LOGO;
			}
/*			else if (cblock[i].logo > 0.10) {
				Debug(2, "Block %i has logo.\n", i);
				Debug(3, "Block %i score:\tBefore - %.2f\t", i, cblock[i].score);
				cblock[i].score *= logo_present_modifier;
//				cblock[i].score *= (logo_present_modifier*cblock[i].logo) + (1-cblock[i].logo);
				cblock[i].score = (cblock[i].score > max_score) ? max_score : cblock[i].score;
				Debug(3, "After - %.2f\n", cblock[i].score);
				cblock[i].cause |= C_LOGO;
				cblock[i].less |= C_LOGO;
			}
*/			else if (punish_no_logo && cblock[i].logo < 0.05 && logoPercentage > logo_fraction){
				Debug(2, "Block %i has no logo.\n", i);
				Debug(3, "Block %i score:\tBefore - %.2f\t", i, cblock[i].score);
				cblock[i].score *= 2;
				cblock[i].score = (cblock[i].score > max_score) ? max_score : cblock[i].score;
				Debug(3, "After - %.2f\n", cblock[i].score);
				cblock[i].cause |= C_LOGO;
				cblock[i].more |= C_LOGO;
			}
		} 
		BuildPunish();
		if (true) {
			if ((punish & 1) && cblock[i].brightness > avg_brightness * punish_threshold) {
				Debug(2, "Block %i is much brighter than average.\n", i);
				Debug(3, "Block %i score:\tBefore - %.2f\t", i, cblock[i].score);
				cblock[i].score *= punish_modifier;
				cblock[i].score = (cblock[i].score > max_score) ? max_score : cblock[i].score;
				Debug(3, "After - %.2f\n", cblock[i].score);
				cblock[i].cause |= C_AB;
				cblock[i].more |= C_AB;
			}
			if ((punish & 2) && cblock[i].uniform > avg_uniform * punish_threshold) {
				Debug(2, "Block %i is less uniform than average.\n", i);
				Debug(3, "Block %i score:\tBefore - %.2f\t", i, cblock[i].score);
				cblock[i].score *= punish_modifier;
				cblock[i].score = (cblock[i].score > max_score) ? max_score : cblock[i].score;
				Debug(3, "After - %.2f\n", cblock[i].score);
				cblock[i].cause |= C_AU;
				cblock[i].more |= C_AU;
			}
			if ((punish & 4) && cblock[i].volume > avg_volume * punish_threshold) {
				Debug(2, "Block %i is much louder than average.\n", i);
				Debug(3, "Block %i score:\tBefore - %.2f\t", i, cblock[i].score);
				cblock[i].score *= punish_modifier;
				cblock[i].score = (cblock[i].score > max_score) ? max_score : cblock[i].score;
				Debug(3, "After - %.2f\n", cblock[i].score);
				cblock[i].cause |= C_AL;
				cblock[i].more |= C_AL;
			}

			if ((punish & 8) && cblock[i].silence > avg_silence * punish_threshold) {
				Debug(2, "Block %i has less silence than average.\n", i);
				Debug(3, "Block %i score:\tBefore - %.2f\t", i, cblock[i].score);
				cblock[i].score *= punish_modifier;
				cblock[i].score = (cblock[i].score > max_score) ? max_score : cblock[i].score;
				Debug(3, "After - %.2f\n", cblock[i].score);
				cblock[i].cause |= C_AS;
				cblock[i].more |= C_AS;
			}
			if ((punish & 16) && cblock[i].schange_count > 2 && cblock[i].schange_rate > avg_schange * punish_threshold) {
				Debug(2, "Block %i has more scene change than average.\n", i);
				Debug(3, "Block %i score:\tBefore - %.2f\t", i, cblock[i].score);
				cblock[i].score *= punish_modifier;
				cblock[i].score = (cblock[i].score > max_score) ? max_score : cblock[i].score;
				Debug(3, "After - %.2f\n", cblock[i].score);
				cblock[i].cause |= C_AC;
				cblock[i].more |= C_AC;
			}
		}
		if (false)
		{
			if ((reward & 1) && cblock[i].brightness < avg_brightness / punish_threshold) {
				Debug(2, "Block %i is much darker than average.\n", i);
				Debug(3, "Block %i score:\tBefore - %.2f\t", i, cblock[i].score);
				cblock[i].score *= reward_modifier;
				cblock[i].score = (cblock[i].score > max_score) ? max_score : cblock[i].score;
				Debug(3, "After - %.2f\n", cblock[i].score);
				cblock[i].cause |= C_BRIGHT;
				cblock[i].less |= C_BRIGHT;
			}
			if ((reward & 2) && cblock[i].uniform < avg_uniform / punish_threshold) {
				Debug(2, "Block %i is more uniform than average.\n", i);
				Debug(3, "Block %i score:\tBefore - %.2f\t", i, cblock[i].score);
				cblock[i].score *= reward_modifier;
				cblock[i].score = (cblock[i].score > max_score) ? max_score : cblock[i].score;
				Debug(3, "After - %.2f\n", cblock[i].score);
				cblock[i].cause |= C_BRIGHT;
				cblock[i].less |= C_BRIGHT;
			}
			if ((reward & 4) && cblock[i].volume < avg_volume / punish_threshold) {
				Debug(2, "Block %i is much quieter than average.\n", i);
				Debug(3, "Block %i score:\tBefore - %.2f\t", i, cblock[i].score);
				cblock[i].score *= reward_modifier;
				cblock[i].score = (cblock[i].score > max_score) ? max_score : cblock[i].score;
				Debug(3, "After - %.2f\n", cblock[i].score);
				cblock[i].cause |= C_BRIGHT;
				cblock[i].less |= C_BRIGHT;
			}
			if ((reward & 8) && cblock[i].silence < avg_silence / punish_threshold) {
				Debug(2, "Block %i has more silence than average.\n", i);
				Debug(3, "Block %i score:\tBefore - %.2f\t", i, cblock[i].score);
				cblock[i].score *= reward_modifier;
				cblock[i].score = (cblock[i].score > max_score) ? max_score : cblock[i].score;
				Debug(3, "After - %.2f\n", cblock[i].score);
				cblock[i].cause |= C_BRIGHT;
				cblock[i].less |= C_BRIGHT;
			}
			if ((reward & 16) && cblock[i].schange_count > 2 && cblock[i].schange_rate < avg_schange / punish_threshold) {
				Debug(2, "Block %i has less scene change than average.\n", i);
				Debug(3, "Block %i score:\tBefore - %.2f\t", i, cblock[i].score);
				cblock[i].score *= reward_modifier;
				cblock[i].score = (cblock[i].score > max_score) ? max_score : cblock[i].score;
				Debug(3, "After - %.2f\n", cblock[i].score);
				cblock[i].cause |= C_BRIGHT;
				cblock[i].less |= C_BRIGHT;
			}
		}

//		cblock[i].logo > 0.5 && (cblock[i].f_end - cblock[i].f_start) > (int) (min_show_segment_length*fps) ))
#if 0		
		// if length < min_show_segment_length, score = 150%
		if (cblock[i].length < min_show_segment_length && cblock[i].logo < 0.2 )) {
			Debug(2, "Block %i is shorter then minimum show segment.\n", i);
			Debug(3, "Block %i score:\tBefore - %.2f\t", i, cblock[i].score);
			cblock[i].score *= 1.5;
			cblock[i].score = (cblock[i].score > max_score) ? max_score : cblock[i].score;
			Debug(3, "After - %.2f\n", cblock[i].score);
		}

#endif
#if 0
		if (framearray && cblock[i].length < max_commercialbreak &&
			cblock[i].brightness < avg_brightness) {
			Debug(2, "Block %i is short but has low brightness.\n", i);
			Debug(3, "Block %i score:\tBefore - %.2f\t", i, cblock[i].score);
			cblock[i].score *= dark_block_modifier;
			cblock[i].score = (cblock[i].score > max_score) ? max_score : cblock[i].score;
			Debug(3, "After - %.2f\n", cblock[i].score);
		}
#endif		
		// if length > max_commercial_size * fps, score = 10%
		if (cblock[i].length > 2 * min_show_segment_length) {
			Debug(2, "Block %i has twice excess length.\n", i);
			Debug(3, "Block %i score:\tBefore - %.2f\t", i, cblock[i].score);
			cblock[i].score *= excessive_length_modifier * excessive_length_modifier;
			cblock[i].score = (cblock[i].score > max_score) ? max_score : cblock[i].score;
			Debug(3, "After - %.2f\n", cblock[i].score);
			cblock[i].cause |= C_EXCEEDS;
			cblock[i].less |= C_EXCEEDS;
		} else

		if (cblock[i].length > min_show_segment_length) {
			Debug(2, "Block %i has excess length.\n", i);
			Debug(3, "Block %i score:\tBefore - %.2f\t", i, cblock[i].score);
			cblock[i].score *= excessive_length_modifier;
			cblock[i].score = (cblock[i].score > max_score) ? max_score : cblock[i].score;
			Debug(3, "After - %.2f\n", cblock[i].score);
			cblock[i].cause |= C_EXCEEDS;
			cblock[i].less |= C_EXCEEDS;
		}

		// Mod score based on scene change rate
/*
		if ( (commDetectMethod & SCENE_CHANGE) && (cblock[i].schange_count > 2) && (cblock[i].length > 3)) {
#if 0
			schange_modifier = (cblock[i].schange_rate / avg_schange);
			schange_modifier = (schange_modifier > min_schange_modifier) ? schange_modifier : min_schange_modifier;
			schange_modifier = (schange_modifier < max_schange_modifier) ? schange_modifier : max_schange_modifier;
			Debug(3, "SC modifier - %.3f\tBlock %i score:\tBefore - %.2f\t", schange_modifier, i, cblock[i].score);
			cblock[i].score *= schange_modifier;
			cblock[i].score = (cblock[i].score > max_score) ? max_score : cblock[i].score;
			Debug(3, "\tSC\tAfter - %.2f\n", cblock[i].score);
#else
			schange_modifier = (cblock[i].schange_rate / avg_schange);
			if (schange_modifier > 2.0 || schange_modifier < 0.5  ) {
				schange_modifier = (schange_modifier > min_schange_modifier) ? schange_modifier : min_schange_modifier;
				schange_modifier = (schange_modifier < max_schange_modifier) ? schange_modifier : max_schange_modifier;
				Debug(3, "SC modifier - %.3f\tBlock %i score:\tBefore - %.2f\t", schange_modifier, i, cblock[i].score);
				cblock[i].score *= schange_modifier;
				cblock[i].score = (cblock[i].score > max_score) ? max_score : cblock[i].score;
				Debug(3, "\tSC\tAfter - %.2f\n", cblock[i].score);
				cblock[i].cause |= C_SC;
			}
#endif

		}
*/
		// Mod score based on CC type
		if (processCC) {
			if (most_cc_type == NONE) {
				if (cblock[i].cc_type != NONE) {
					Debug(3, "CC's exist in a non-CC'd show - Block %i score:\tBefore - %.2f\t", i, cblock[i].score);
					cblock[i].score *= cc_commercial_type_modifier * 2;
					Debug(3, "After - %.2f\n", cblock[i].score);
					cblock[i].score = (cblock[i].score > max_score) ? max_score : cblock[i].score;
				}
			} else {
				if (cblock[i].cc_type == most_cc_type) {
					Debug(3, "CC's correct type - Block %i score:\tBefore - %.2f\t", i, cblock[i].score);
					cblock[i].score *= cc_correct_type_modifier;
					Debug(3, "After - %.2f\n", cblock[i].score);
					cblock[i].score = (cblock[i].score > max_score) ? max_score : cblock[i].score;
				} else if (cblock[i].cc_type == COMMERCIAL) {
					Debug(3, "CC's commercial type - Block %i score:\tBefore - %.2f\t", i, cblock[i].score);
					cblock[i].score *= cc_commercial_type_modifier;
					Debug(3, "After - %.2f\n", cblock[i].score);
					cblock[i].score = (cblock[i].score > max_score) ? max_score : cblock[i].score;
				} else if (cblock[i].cc_type == NONE) {
					Debug(3, "No CC's - Block %i score:\tBefore - %.2f\t", i, cblock[i].score);
					cblock[i].score *= (((cc_wrong_type_modifier-1.0)/2)+1.0);
					Debug(3, "After - %.2f\n", cblock[i].score);
					cblock[i].score = (cblock[i].score > max_score) ? max_score : cblock[i].score;
				} else {
					Debug(3, "CC's wrong type - Block %i score:\tBefore - %.2f\t", i, cblock[i].score);
					cblock[i].score *= cc_wrong_type_modifier;
					Debug(3, "After - %.2f\n", cblock[i].score);
					cblock[i].score = (cblock[i].score > max_score) ? max_score : cblock[i].score;
				}
			}
		}

		// Mod score based on AR
//		if (commDetectMethod & AR) {
			cblock[i].ar_ratio = AverageARForBlock(cblock[i].f_start, cblock[i].f_end);
			if ((dominant_ar - cblock[i].ar_ratio >= ar_delta ||
				dominant_ar - cblock[i].ar_ratio <= - ar_delta)
//				cblock[i].length < min_show_segment_length 
//				&& (cblock[i].length > 5.0 || cblock[i].ar_ratio - ar_delta < dominant_ar)
				) {
				Debug(2, "Block %i AR (%.2f) is different from dominant AR(%.2f).\n",i,cblock[i].ar_ratio, dominant_ar);
				Debug(3, "Block %i score:\tBefore - %.2f\t", i, cblock[i].score);
				cblock[i].score *= ar_wrong_modifier;
				Debug(3, "After - %.2f\n", cblock[i].score);
				cblock[i].cause |= C_AR;
				cblock[i].more |= C_AR;
			}			
//		}
	}

	if (processCC) {
		if (ProcessCCDict()) {
			Debug(4, "Dictionary processed successfully\n");
		} else {
			Debug(4, "Dictionary not processed successfully\n");
		}
	}
	for (i = 0; i < block_count; i++){
//		OutputStrict(cblock[i].length, (double) cblock[i].strict, 0.0);
	}


	if (!(disable_heuristics & (1 << (2 - 1)))) {
	for (i = 0; i < block_count-2; i++) {
		if ( ((cblock[i].cause & C_STRICT) && (cblock[i].cause & (C_b | C_u | C_v | C_r)) )  && 
			 cblock[i+1].score > 1.05 &&  cblock[i+1].length < 4.8 && 
			 cblock[i+2].score < 1.0  &&  cblock[i+2].length > min_show_segment_length
			 ) {
			cblock[i+1].score = 0.5;
			Debug(3, "H2 Added cblock %i because short and after strict commercial.\n", i+1);
			cblock[i+1].cause |= C_H2;
			cblock[i+1].less |= C_H2;
		}
	}
	for (i = 0; i < block_count-2; i++) {
		if ( ((cblock[i+2].cause & C_STRICT) && (cblock[i+1].cause & (C_b | C_u | C_v | C_r)) )  && 
			 cblock[i+1].score > 1.05 &&  cblock[i+1].length < 4.8 && 
			 cblock[i].score < 1.0  &&  cblock[i].length > min_show_segment_length
			 ) {
			cblock[i+1].score = 0.5;
			Debug(3, "H2 Added cblock %i because short and before strict commercial.\n", i+1);
			cblock[i+1].cause |= C_H2;
			cblock[i+1].less |= C_H2;
		}
	}

	for (i = 0; i < block_count-2; i++) {
		if ( (cblock[i].cause & (C_b | C_u | C_r))  && (cblock[i+1].cause & C_a)  &&
			 cblock[i+1].score > 1.0 &&  cblock[i+1].length < 4.8 && 
			 cblock[i+2].score < 1.0  &&  cblock[i+2].length > min_show_segment_length
			 ) {
			cblock[i+1].score = 0.5;
			Debug(3, "H2 Added cblock %i because short and based on aspect ratio change after commercial.\n", i+1);
			cblock[i+1].cause |= C_H2;
			cblock[i+1].less |= C_H2;
		}
	}
	for (i = 0; i < block_count-2; i++) {
		if ( (cblock[i+1].cause & (C_b | C_u | C_r))  && (cblock[i].cause & C_a)  &&
			 cblock[i+1].score > 1.0 &&  cblock[i+1].length < 4.8 && 
			 cblock[i].score < 1.0  &&  cblock[i].length > min_show_segment_length
			 ) {
			cblock[i+1].score = 0.5;
			Debug(3, "H2 Added cblock %i because short and based on aspect ratio change before commercial.\n", i+1);
			cblock[i+1].cause |= C_H2;
			cblock[i+1].less |= C_H2;
		}
	}

	}

	if (!(disable_heuristics & (1 << (1 - 1)))) {
	for (i = 0; i < block_count-1; i++) {
		if (cblock[i].score > 1.4 && cblock[i+1].score <= 1.05 &&  cblock[i+1].score > 0.0) {
			combined_length = 0;						
			wscore = 0;
			lscore = 0;
			j = i+1;
			while (j < block_count && combined_length < min_show_segment_length && cblock[j].score <= 1.05 && cblock[j].score > 0.0) {
				combined_length += cblock[j].length;
				wscore += cblock[j].length * cblock[j].score;
				lscore += cblock[j].length * cblock[j].logo;
				j++;
			}
			wscore /= combined_length;
			lscore /= combined_length;
			if (//lscore < 0.36 &&
				((combined_length < min_show_segment_length / 1.0 && wscore > 0.9) ||
				 (combined_length < min_show_segment_length / 1.5 && wscore > 0.3) ) &&
				 cblock[j].score > 1.4 &&
				(combined_length < min_show_segment_length / 3 || 
				(cblock[i].f_start > after_start &&
				 cblock[j].f_end < before_end))) { 
				for (k = i+1; k < j; k++) {
					cblock[k].score = 99.99;
					Debug(3, "H1 Discarding cblock %i because too short and between two commercial blocks.\n",
						k);
					cblock[k].cause |= C_H1;
					cblock[k].more |= C_H1;
				}

			}

		}
	}
	}
	
	
	/*	
	for (i = 0; i < block_count-2; i++) {
		if (cblock[i].score < 0.9 && cblock[i+1].score == 1.0 && cblock[i+1].length < min_show_segment_length/2 && cblock[i+2].score > 1.5) {
			cblock[i+1].score *= 1.5;
			Debug(3, "Discarding cblock %i because short and on edge between commercial and show.\n",
					i+1);
		}
		if (cblock[i].score > 1.5 && cblock[i+1].score == 1.0 && cblock[i+1].length < min_show_segment_length/2 && cblock[i+2].score < 0.9) {
			cblock[i+1].score *= 1.5;
			Debug(3, "Discarding cblock %i because short and on edge between commercial and show.\n",
					i+1);
		}
	}
*/	

/*	
	if (delete_show_before_or_after_current && logoPercentage == 0) { 
		i = 0;
		while (i < block_count-1 && cblock[i].score < 1.0 && cblock[i].f_end < before_end) {
			j = i+1;
			k = 0;
			while (cblock[j].score > 1.05 && k + cblock[j].length * fps < min_commercialbreak * fps && j < block_count-1) {
				k += (int)(cblock[j].length * fps);
				j++;
			}
			if (cblock[j].score < 1.0) {
				cblock[i].score = 99.99;
				Debug(3, "Discarding cblock %i because separated from cblock %i with small non show gap.\n",
					i, j);
				cblock[i].cause |= C_H2;
				cblock[i].more |= C_H2;
				start_deleted = true;
				break;
			}
			i++;
		}
		if (! start_deleted) {
			j = 0;
			i = 0;
			if (cblock[j].score < 1.0) {
				cblock[i].score = 99.99;
				Debug(3, "Discarding cblock %i because of being first block.\n",
					i, j);
				cblock[i].cause |= C_H2;
				cblock[i].more |= C_H2;
				start_deleted = true;
			}
		}
		i = block_count-1;
		while (i > 0 && cblock[i].score < 1.05 && cblock[i].f_start > before_end) {
			j = i-1;
			k = 0;
			while (cblock[j].score > 1.05 && k + cblock[j].length * fps < min_commercialbreak * fps && j >0) {
				k += (int)(cblock[j].length * fps);
				j--;
			}
			if (cblock[j].score < 1.0) {
				cblock[i].score = 99.99;
				Debug(3, "Discarding cblock %i because seprated from cblock %i with small non show gap.\n",
					i, j);
				cblock[i].cause |= C_H2;
				cblock[i].more |= C_H2;
				end_deleted = true;
				break;
			}
			i++;
		}
		
		if (! end_deleted) {
			i = block_count-1;
			j = block_count-1;
			if (cblock[j].score < 1.05) {
				cblock[i].score = 99.99;
				Debug(3, "Discarding cblock %i because being last block.\n",
					i, j);
				cblock[i].cause |= C_H2;
				cblock[i].more |= C_H2;
				end_deleted = true;
			}
		}
	}
*/
	if (delete_show_before_or_after_current && logo_block_count >= 40)
		Debug(10, "Too many logo blocks, disabling the delete_show_before_or_after_current processing\n");
	if (delete_show_before_or_after_current && 
		(commDetectMethod & LOGO) && connect_blocks_with_logo &&
		!reverseLogoLogic && logoPercentage > logo_fraction - 0.05 && logo_block_count < 40) {
/*		
		for (i = 0; i < block_count-1; i++) {
			if (cblock[i].score < 1.0 && cblock[i].logo > 0.2 && cblock[i+1].score < 1.0 && cblock[i+1].logo > 0.2 ) {
				if (cblock[i].f_end < after_start) {
					cblock[i].score = 99.99;
					Debug(3, "Discarding cblock %i because cblock %i has also logo.\n",
						i, i+1);
				} else if (cblock[i+1].f_start > before_end) { 
					cblock[i+1].score = 99.99;
					Debug(3, "Discarding cblock %i because cblock %i has also logo.\n",
						i+1, i);
				}
			}
		}
	*/
	if (!(disable_heuristics & (1 << (7 - 1)))) {
		i = 0;
		while (i < block_count-1 && cblock[i].score < 1.05 && 
				((delete_show_before_or_after_current == 1 && cblock[i].f_end < after_start) ||
				 (delete_show_before_or_after_current > 1 && delete_show_before_or_after_current > cblock[i].length))) {
			j = i+1;
			k = 0;
			combined_length = 0;
			while (cblock[j].score > 1.05 && k + cblock[j].length * fps < min_commercialbreak * fps && j < block_count-1) {
				combined_length += cblock[j].length;
				k += (int)(cblock[j].length * fps);
				j++;
			}
			if (cblock[j].score < 1.0 && cblock[j].length > min_show_segment_length/2 ) {
				cblock[i].score = 99.99;
				Debug(3, "H7 Discarding cblock %i of %i seconds because cblock %i has also logo and small non show gap.\n",
						i, (int)cblock[i].length, j);
				cblock[i].cause |= C_H7;
				cblock[i].more |= C_H7;
				start_deleted = true;
				break;
			}
			i++;
		}
		
		i = block_count-1;
		while (i > 0 && cblock[i].score < 1.0 && 
				((delete_show_before_or_after_current == 1 && cblock[i].f_start > before_end) ||
				 (delete_show_before_or_after_current > 1 && delete_show_before_or_after_current > cblock[i].length))) {
			j = i-1;
			k = 0;
			combined_length = 0;
			while (cblock[j].score > 1.05 && k + cblock[j].length * fps < min_commercialbreak * fps && j >0) {
				combined_length += cblock[j].length;
				k += (int)(cblock[j].length * fps);
				j--;
			}
			if (cblock[j].score < 1.0) {
				cblock[i].score = 99.99;
				Debug(3, "H7 Discarding cblock %i of %i seconds because cblock %i has also logo and small non show gap.\n",
						i, (int)cblock[i].length, j);
				cblock[i].cause |= C_H7;
				cblock[i].more |= C_H7;
				end_deleted = true;
				break;
			}
			i++;
		}
	}		
/*		
		for (i = 0; i < block_count-1; i++) {
			if (cblock[i].score < 1.0 

//				&& cblock[i+1].score > 1.05 && cblock[i+1].length < 10 && cblock[i+2].score < 1.0 && cblock[i+2].logo > 0.2
				) {
				j = i+1;
				k = 0;
				while (cblock[j].score > 1.05 && k + cblock[j].length * fps < min_commercialbreak * fps && j < block_count-1) {
					k += (int)(cblock[j].length * fps);
					j++;
				}
				if (cblock[j].score < 1.0) {
					if (cblock[j].f_start < after_start) {
						cblock[i].score = 99.99;
						Debug(3, "Discarding cblock %i because cblock %i has also logo and small non logo gap.\n",
							i, j);
					} else if (cblock[i].f_end > before_end) { 
						cblock[j].score = 99.99;
						Debug(3, "Discarding cblock %i because cblock %i has also logo and small non logo gap.\n",
							j, i);
					}
				}
			}
		}
*/
	if (!(disable_heuristics & (1 << (3 - 1)))) {
		for (i = 0; i < block_count-1; i++) {
			if (cblock[i].score < 1.0 && cblock[i].logo < 0.1 && cblock[i].length > min_show_segment_length ) {
				if (cblock[i].f_end < after_start) {
					cblock[i].score *= 1.3;
					Debug(3, "H3 Demoting cblock %i because cblock %i has no logo and others do.\n",
						i, i);
					cblock[i].cause |= C_H3;
					cblock[i].more |= C_H3;

				} else if (cblock[i].f_start > before_end) { 
					cblock[i].score *= 1.3;
					Debug(3, "Demoting cblock %i because cblock %i has no logo and others do.\n",
						i, i);
					cblock[i].cause |= C_H3;
					cblock[i].more |= C_H3;

				}
			}
		}

	}
//	if (!(disable_heuristics & (1 << (3 - 1)))) {
		for (i = 1; i < block_count-1; i++) {
			if (logoPercentage > logo_fraction && 
				cblock[i].score > 1.0 && cblock[i].logo < 0.1 && 
				cblock[i].length > min_show_segment_length+4 ) {
				if (cblock[i].f_start > after_start &&
					cblock[i].f_end   < before_end &&
					(cblock[i-1].score < 1.0 || cblock[i+1].score < 1.0 )) { 
					cblock[i].score *= 0.5;
					Debug(3, "Promoting cblock %i because cblock %i has no logo but long and in the middle of a show.\n",
						i, i);
					cblock[i].cause |= C_H3;
					cblock[i].more |= C_H3;

				}
			}
		}

//	}
}
	if (!(disable_heuristics & (1 << (4 - 1)))) {
	
	if ((commDetectMethod & LOGO) && !reverseLogoLogic && logoPercentage > logo_fraction) {
		i = 1;
		while (i < block_count) {
			if (cblock[i].score < 1 && cblock[i].b_head > 7 && CUTCAUSE(cblock[i-1].cause) == C_b) {
				j = i-1;
				k = 0;
				while (j >= 0 && k < 5 && cblock[j].b_head > 7 && cblock[j].length < 7 && CUTCAUSE(cblock[j].cause) == C_b) {
					cblock[j].score *= 0.1;   //  Add blocks with long black periods before show
					Debug(3, "H4 Added cblock %i because of large black gap with cblock %i\n", j, i);
					k++;
					cblock[j].cause |= C_H4;
					cblock[j].less |= C_H4;
					j--;
				}
			}
			i++;
		}
	}
	if ((commDetectMethod & LOGO) && !reverseLogoLogic && logoPercentage > logo_fraction) {
		i = 0;
		while (i < block_count) {
			if (cblock[i].score < 1 && cblock[i].b_tail > 7 && CUTCAUSE(cblock[i].cause) == C_b) {
				j = i+1;
				k = 0;
				while (j < block_count && k < 5 && cblock[j].b_tail > 7 && cblock[j].length < 7 && CUTCAUSE(cblock[j-1].cause) == C_b) {
					cblock[j].score *= 0.1;   //  Add blocks with long black periods before show
					Debug(3, "H4 Added cblock %i because of large black gap with cblock %i\n", j, i);
					k++;
					cblock[j].cause |= C_H4;
					cblock[j].less |= C_H4;
					j++;
				}
			}
			i++;
		}
	}
	}
	if (!(disable_heuristics & (1 << (2 - 1)))) {
/*		i = 0;
		k = 0;
		while (cblock[i].score < 1.05 && k + cblock[i].length * fps < min_commercialbreak * fps && i < block_count-1) {
			k += (int)(cblock[i].length * fps);
			i++;
		}
		if (i < block_count-1 && cblock[i].score > 1.05 && k < min_commercialbreak * fps) {		
			for (j = 0; j < i; j++) {
				cblock[j].score = 99.99;
				Debug(3, "H2 Discarding cblock %i because too short and before commercial.\n",
					j);
				cblock[j].cause |= C_H2;
				cblock[j].more |= C_H2;
			}
		}
*/
	}
/*

	if (!(disable_heuristics & (1 << (1 - 1)))) {
	
	for (i = 0; i < block_count-2; i++) {
		if (cblock[i].score < 1.05 && cblock[i+1].score > 1.05 && cblock[i+2].score < 1.05 &&
			cblock[i+1].length > min_show_segment_length && logoPercentage < 0.7) {
			j = i + 2;
			for (k = i+1; k < j; k++) {
					cblock[k].score = 0.05;
					Debug(3, "H1 Included cblock %i because too long and between two show blocks.\n",
						k);
					cblock[k].cause |= C_H1;
					cblock[k].more |= C_H1;
			}

		}
	}
	}
*/
}
/*
	for (i = 0; i < block_count-2; i++) {
		if (cblock[i].score < 0.9 && cblock[i+1].score > 1.0 && cblock[i+2].score > 1.5 && 
			!(cblock[i].cause & (C_b | C_u | C_v | C_a)) ) {
			cblock[i+1].score = 0.5;
			Debug(3, "Eroded cblock %i because vague cut reason and on edge between commercial and show.\n",
					i+1);
		}
		if (cblock[i].score > 1.5 && cblock[i+1].score > 1.0 && cblock[i+2].score < 0.9 &&
			!(cblock[i+1].cause & (C_b | C_u | C_v | C_a)) ) {
			cblock[i+1].score = 0.5;
			Debug(3, "Eroded cblock %i because vague cut reason and on edge between commercial and show.\n",
					i+1);
		}
	}
*/




#ifndef _WIN32
#include <ctype.h>
char *_strupr(char *string)
{
      char *s;

      if (string)
      {
            for (s = string; *s; ++s)
                  *s = toupper(*s);
      }
      return string;
} 
#endif


void OpenOutputFiles()
{
	char	tempstr[MAX_PATH];
	char	cwd[MAX_PATH];

	if (output_default) {
		out_file = fopen(out_filename, "w");
		if (!out_file) {
			Sleep(50L);
			out_file = fopen(out_filename, "w");
			if (!out_file) {
				Debug(0, "ERROR writing to %s\n", out_filename);
				exit(103);
			}
		}
		fprintf(out_file, "FILE PROCESSING COMPLETE %6li FRAMES AT %4i\n-------------------\n",frame_count-1, (int)(fps*100));
		fclose(out_file);
	}

	if (output_chapters) {
		sprintf(filename, "%s.chap", basename);
		chapters_file = fopen(filename, "w");
		if (!chapters_file) {
			Sleep(50L);
			out_file = fopen(chapters_file, "w");
			if (!chapters_file) {
				Debug(0, "ERROR writing to %s\n", filename);
				exit(103);
			}
		}
		fprintf(chapters_file, "FILE PROCESSING COMPLETE %6li FRAMES AT %4i\n-------------------\n",frame_count-1, (int)(fps*100));
	}

	if (output_zoomplayer_cutlist) {
		sprintf(filename, "%s.cut", basename);
		zoomplayer_cutlist_file = fopen(filename, "w");
		if (!zoomplayer_cutlist_file) {
			fprintf(stderr, "%s - could not create file %s\n", strerror(errno), filename);
			exit(6);
		} else {
			output_zoomplayer_cutlist = true;
//			fclose(zoomplayer_cutlist_file);
		}
  }
	if (output_plist_cutlist) {
		sprintf(filename, "%s.plist", basename);
		plist_cutlist_file = fopen(filename, "w");
		if (!plist_cutlist_file) {
			fprintf(stderr, "%s - could not create file %s\n", strerror(errno), filename);
			exit(6);
		} else {
			output_plist_cutlist = true;
      fprintf(plist_cutlist_file, "<array>\n");
//			fclose(plist_cutlist_file);
		}
  }

	if (output_incommercial) {
		sprintf(filename, "%s.incommercial", basename);
		incommercial_file = fopen(filename, "w");
		if (!incommercial_file) {
			fprintf(stderr, "%s - could not create file %s\n", strerror(errno), filename);
			exit(6);
		}
      fprintf(incommercial_file, "0\n");
	  fclose(incommercial_file);
		}




	if (output_zoomplayer_chapter) {
		sprintf(filename, "%s.chp", basename);
		zoomplayer_chapter_file = fopen(filename, "w");
		if (!zoomplayer_chapter_file) {
			fprintf(stderr, "%s - could not create file %s\n", strerror(errno), filename);
			exit(6);
		} else {
			output_zoomplayer_chapter = true;
//			fclose(zoomplayer_chapter_file);
		}
	}

	if (output_edl) {
		sprintf(filename, "%s.edl", basename);
		edl_file = fopen(filename, "wb");
		if (!edl_file) {
			fprintf(stderr, "%s - could not create file %s\n", strerror(errno), filename);
			exit(6);
		} else {
			output_edl = true;
		}
	}

	if (output_ipodchap) {
		sprintf(filename, "%s.chap", basename);
		ipodchap_file = fopen(filename, "w");
		if (!ipodchap_file) {
			fprintf(stderr, "%s - could not create file %s\n", strerror(errno), filename);
			exit(6);
		} else {
			output_ipodchap = true;
		}
		fprintf(ipodchap_file,"CHAPTER01=00:00:00.000\nCHAPTER01NAME=1\n");
	}

	if (output_edlp) {
		sprintf(filename, "%s.edlp", basename);
		edlp_file = fopen(filename, "w");
		if (!edlp_file) {
			fprintf(stderr, "%s - could not create file %s\n", strerror(errno), filename);
			exit(6);
		} else {
			output_edlp = true;
		}
	}


	if (output_bsplayer) {
		sprintf(filename, "%s.bcf", basename);
		bcf_file = fopen(filename, "w");
		if (!bcf_file) {
			fprintf(stderr, "%s - could not create file %s\n", strerror(errno), filename);
			exit(6);
		} else {
			output_bsplayer = true;
		}
	}

	if (output_edlx) {
		sprintf(filename, "%s.edlx", basename);
		edlx_file = fopen(filename, "w");
		if (!edlx_file) {
			fprintf(stderr, "%s - could not create file %s\n", strerror(errno), filename);
			exit(6);
		} else {
			output_edlx = true;
			fprintf(edlx_file, "<regionlist units=\"bytes\" mode=\"exclude\"> \n");
		}
	}


	if (output_videoredo) {
//<Version>2
//<Filename>G:\comskip79_46\mpg\MXC_20060518_00000030.mpg
//<Cut>4255584667:5666994667
//<Cut>8590582000:11001991000
//<SceneMarker 0>797115333
//<SceneMarker 1>1083729555
//<SceneMarker 2>4254502333
//<SceneMarker 3>4708947222

		sprintf(filename, "%s.VPrj", basename);
		videoredo_file = fopen(filename, "w");
		if (videoredo_file) {
			if (mpegfilename[1] == ':' || mpegfilename[0] == '\\') {
				fprintf(videoredo_file, "<Version>2\n<Filename>%s\n", mpegfilename);
			} else {
				_getcwd(cwd, 256);
				fprintf(videoredo_file, "<Version>2\n<Filename>%s\\%s\n", cwd, mpegfilename);
			}
			if (is_h264) {
				fprintf(videoredo_file, "<MPEG Stream Type>4\n");
			}

//			fclose(videoredo_file);
			output_videoredo = true;
		} else {
			fprintf(stderr, "%s - could not create file %s\n", strerror(errno), filename);
			exit(6);
		}
	}

	if (output_btv) {
		sprintf(filename, "%s.chapters.xml", mpegfilename);
		btv_file = fopen(filename, "w");
		if (btv_file) {
			fprintf(btv_file, "<cutlist>\n");
//			fclose(btv_file);
			output_btv = true;
		} else {
			fprintf(stderr, "%s - could not create file %s\n", strerror(errno), filename);
			exit(6);
		}
	}

	if (output_cuttermaran) {
		sprintf(filename, "%s.cpf", basename);
		cuttermaran_file = fopen(filename, "w");
		if (cuttermaran_file) {
			if (mpegfilename[1] == ':' || mpegfilename[0] == '\\') {
				strcpy(tempstr, basename);
			} else {
				_getcwd(cwd, 256);
				sprintf(tempstr, "%s\\%s", cwd, basename);
			}
			fprintf(cuttermaran_file, "<?xml version=\"1.0\" standalone=\"yes\"?>\n");
			fprintf(cuttermaran_file, "<StateData xmlns=\"http://cuttermaran.kickme.to/StateData.xsd\">\n");
			fprintf(cuttermaran_file, "<usedVideoFiles FileID=\"0\" FileName=\"%s.M2V\" />\n",basename);
			fprintf(cuttermaran_file, "<usedAudioFiles FileID=\"1\" FileName=\"%s.mp2\" StartDelay=\"0\" />\n",basename);
//			fclose(cuttermaran_file);
		} else {
			fprintf(stderr, "%s - could not create file %s\n", strerror(errno), filename);
			exit(6);
		}
	}

	if (output_vcf) {
		sprintf(filename, "%s.vcf", basename);
		vcf_file = fopen(filename, "w");
		if (vcf_file) {
			if (mpegfilename[1] == ':' || mpegfilename[0] == '\\') {
				strcpy(tempstr, basename);
			} else {
				_getcwd(cwd, 256);
				sprintf(tempstr, "%s\\%s", cwd, basename);
			}
			fprintf(vcf_file, "VirtualDub.video.SetMode(0);\nVirtualDub.subset.Clear();\n");
//			fclose(vcf_file);
		} else {
			fprintf(stderr, "%s - could not create file %s\n", strerror(errno), filename);
			exit(6);
		}
	}

	if (output_vdr) {
		sprintf(filename, "%s.vdr", basename);
		vdr_file = fopen(filename, "w");
		if (vdr_file) {
			if (mpegfilename[1] == ':' || mpegfilename[0] == '\\') {
				strcpy(tempstr, basename);
			} else {
				_getcwd(cwd, 256);
				sprintf(tempstr, "%s\\%s", cwd, basename);
			}
//			fprintf(vdr_file, "VirtualDub.video.SetMode(0);\nVirtualDub.subset.Clear();\n");
//			fclose(vdr_file);
		} else {
			fprintf(stderr, "%s - could not create file %s\n", strerror(errno), filename);
			exit(6);
		}
	}

	if (output_projectx) {
		sprintf(filename, "%s.Xcl", mpegfilename);
		projectx_file = fopen(filename, "w");
		if (projectx_file) {
			fprintf(projectx_file, "CollectionPanel.CutMode=2\n");
		} else {
			fprintf(stderr, "%s - could not create file %s\n", strerror(errno), filename);
			exit(6);
		}
	}

	if (output_avisynth) {
		sprintf(filename, "%s.avs", mpegfilename);
		avisynth_file = fopen(filename, "w");
		if (avisynth_file) {
			if (avisynth_options[0] == 0)	
				fprintf(avisynth_file, "LoadPlugin(\"MPEG2Dec3.dll\") \nMPEG2Source(\"%s\")\n", mpegfilename);
			else
				fprintf(avisynth_file, avisynth_options, mpegfilename);

		} else {
			fprintf(stderr, "%s - could not create file %s\n", strerror(errno), filename);
			exit(6);
		}
	}

	if (output_womble) {
		sprintf(filename, "%s.wme", basename);
		womble_file = fopen(filename, "w");
		if (womble_file) {
//			fclose(womble_file);
			output_womble = true;
		} else {
			fprintf(stderr, "%s - could not create file %s\n", strerror(errno), filename);
			exit(6);
		}
	}

	if (output_mls) {
		sprintf(filename, "%s.mls", basename);
		mls_file = fopen(filename, "w");
		if (mls_file) {
//			fclose(mls_file);
			output_mls = true;
//[BookmarkList]
//PathName= C:\VidTst\Will - Grace - Secrets - Lays.mpg
//VideoStreamID= 224
//Format= frame
//Count= 19

		} else {
			fprintf(stderr, "%s - could not create file %s\n", strerror(errno), filename);
			exit(6);
		}
	}

	if (output_mpgtx) {
		sprintf(filename, "%s_mpgtx.bat", basename);
		mpgtx_file = fopen(filename, "w");
		if (mpgtx_file) {
//			fclose(mpgtx_file);
			output_mpgtx = true;
			fprintf(mpgtx_file, "mpgtx.exe -j -f -o \"%s%s\" \"%s\" ", mpegfilename, ".clean", mpegfilename);
		} else {
			fprintf(stderr, "%s - could not create file %s\n", strerror(errno), filename);
			exit(6);
		}
	}

	if (output_dvrcut) {
		sprintf(filename, "%s_dvrcut.bat", basename);
		dvrcut_file = fopen(filename, "w");
		if (dvrcut_file) {
//			fclose(dvrcut_file);
			if (dvrcut_options[0] == 0)	
				fprintf(dvrcut_file, "dvrcut \"%%1\" \"%%2\" ");
			else
				fprintf(dvrcut_file, dvrcut_options, basename, basename, basename  );
		} else {
			fprintf(stderr, "%s - could not create file %s\n", strerror(errno), filename);
			exit(6);
		}
	}

	if (output_dvrmstb) {
		if (outputfilename[0])
			sprintf(filename, "%s\\%s.xml", outputfilename, shortbasename);
		else
			sprintf(filename, "%s.xml", basename);
		dvrmstb_file = fopen(filename, "w");
		if (dvrmstb_file) {
//			fclose(dvrmstb_file);
			fprintf(dvrmstb_file, "<?xml version=\"1.0\" encoding=\"utf-8\" ?>\n<root>\n");
		} else {
			fprintf(stderr, "%s - could not create file %s\n", strerror(errno), filename);
			exit(6);
		}
	}

	if (output_mpeg2schnitt) {
		sprintf(filename, "%s_mpeg2schnitt.bat", basename);
		mpeg2schnitt_file = fopen(filename, "w");
		if (mpeg2schnitt_file) {
//			fclose(mpeg2schnitt_file);
			output_mpgtx = true;
// Mpeg2Schnitt.exe %1.m2v /R29.97 /o250 /i550 /o3210 /i4000 /S /E /Z %2.m2v 
			if (mpeg2schnitt_options[0] == 0)	
				fprintf(mpeg2schnitt_file, "mpeg2schnitt.exe /S /E /R%5.2f  /Z \"%s\" \"%s\" ", fps, "%2", "%1");
			else
				fprintf(mpeg2schnitt_file, "%s ", mpeg2schnitt_options);
		} else {
			fprintf(stderr, "%s - could not create file %s\n", strerror(errno), filename);
			exit(6);
		}
	}
}

#define CLOSEOUTFILE(F) { if ((F) && last) fclose(F); } 

void OutputCommercialBlock(int i, long prev, long start, long end, bool last) {
	int s_start, s_end;
	int count;
	double minutes = frame_count / (60 * fps);
	char scomment[80];
	char ecomment[80];

	start = max(start,0);
	end = max(end,0);
	
	s_start = start;
	s_end = end;

	if (sage_minute_bug) {
		s_start = (int)(start * (((int)( minutes+0.5))/minutes));
		s_end = (int)(end * (((int)(minutes+0.5))/minutes));
	}
	if (output_default && prev < start /*&& !last */) {
		out_file = fopen(out_filename, "a+");
		if (out_file) {
			fprintf(out_file, "%i\t%i\n", (sage_framenumber_bug?s_start/2:s_start), (sage_framenumber_bug?s_end/2:s_end));
			fclose(out_file);
		} else {		// If the file can't be opened for writting, wait half a second and try again
			Sleep(50L);
			out_file = fopen(out_filename, "a+");
			if (out_file) {
				fprintf(out_file, "%i\t%i\n", (sage_framenumber_bug?s_start/2:s_start), (sage_framenumber_bug?s_end/2:s_end));
				fclose(out_file);
			} else {	// If the file still can't be opened for writting, give up and exit
				Debug(0, "ERROR writing to %s\n", out_filename);
				exit(103);
			}
		}		
	}
	//CLOSEOUTFILE(out_file);

	if (zoomplayer_cutlist_file && prev < start && end - start > 2) {
		fprintf(zoomplayer_cutlist_file, "JumpSegment(\"From=%.4f\",\"To=%.4f\")\n", (start) / fps, (end) / fps);
	}
	CLOSEOUTFILE(zoomplayer_cutlist_file);
	if (plist_cutlist_file) {
		if (prev < start /* &&!last */) {
      // NOTE: we could possibly simplify this to just printing start and end without the math
			fprintf(plist_cutlist_file, "<integer>%lld</integer> <integer>%lld</integer>\n", 
              (unsigned long)((start) / fps * 90000), (unsigned long)((end) / fps * 90000));
		}
		if (last) {
			fprintf(plist_cutlist_file, "</array>\n");
		}
	}
	CLOSEOUTFILE(plist_cutlist_file);

	if (zoomplayer_chapter_file && prev < start && end - start > fps ) {
//		fprintf(zoomplayer_chapter_file, "AddChapterBySecond(%.4f,Commercial Segment)\nAddChapterBySecond(%.4f,Show Segment)\n", (start) / fps, (end) / fps);
		fprintf(zoomplayer_chapter_file, "AddChapterBySecond(%i,Commercial Segment)\nAddChapterBySecond(%i,Show Segment)\n", (int)((start) / fps), (int)((end) / fps));
	}
	CLOSEOUTFILE(zoomplayer_chapter_file);

	if (vcf_file && prev < start && start - prev > 5 && prev > 0 ) {
		fprintf(vcf_file, "VirtualDub.subset.AddRange(%i,%i);\n", prev-1, start - prev);
	}
	CLOSEOUTFILE(vcf_file);

	if (vdr_file && prev < start && end - start > 2) {
		if (start < 5)
			start = 0;
		fprintf(vdr_file, "%s start\n",	dblSecondsToStrMinutes(start/fps ));
		fprintf(vdr_file, "%s end\n", dblSecondsToStrMinutes(end/fps));
	}
	CLOSEOUTFILE(vdr_file);

	if (projectx_file && prev < start) {
		fprintf(projectx_file, "%ld\n", prev+1);
		fprintf(projectx_file, "%ld\n", start);
	}
	CLOSEOUTFILE(projectx_file);

	if (avisynth_file && prev < start) {
		fprintf(avisynth_file, "%strim(%ld,", (prev < 10 ? "" : " ++ "), prev+1);
		fprintf(avisynth_file, "%ld)", start);
	}
	if (avisynth_file && last) {
		fprintf(avisynth_file, "\n");
	}
	CLOSEOUTFILE(avisynth_file);

	if (videoredo_file && prev < start && end - start > 2) {
		if (i == 0 && demux_pid)
			fprintf(videoredo_file, "<VideoStreamPID>%d\n<AudioStreamPID>%d\n", selected_video_pid, selected_audio_pid);
		s_start = max(start-videoredo_offset-1,0);
		s_end = max(end - videoredo_offset-1,0);
		fprintf(videoredo_file, "<Cut>%.0f:%.0f\n", s_start / fps * 10000000, s_end / fps * 10000000);
	}
	CLOSEOUTFILE(videoredo_file);

	if (btv_file && prev < start) {
		strcpy(scomment, dblSecondsToStrMinutes(start / fps));
		strcpy(ecomment, dblSecondsToStrMinutes(end / fps));

		fprintf(btv_file, "<Region><start comment=\"%s\">%.0f</start><end comment=\"%s\">%.0f</end></Region>\n", 
			scomment, start / fps * 10000000, ecomment, end / fps * 10000000);
		if (last) {
			fprintf(btv_file, "</cutlist>\n");
		}
	}
	CLOSEOUTFILE(btv_file);

	if (edl_file && prev < start /* &&!last */ && end - start > 2) {
		if (start < 5)
			start = 0;
		s_start = max(start-edl_offset,0);
		s_end = max(end - edl_offset,0);

		if (demux_pid && enable_mencoder_pts) {
			fprintf(edl_file, "%.2f\t%.2f\t0\n", (double)s_start / fps + initial_pts/ 90000.0, (double)s_end / fps  + initial_pts/ 90000.0);
		} else {
			fprintf(edl_file, "%.2f\t%.2f\t0\n", (double)s_start / fps , (double)s_end / fps );
		}
	}
	CLOSEOUTFILE(edl_file);

	if (ipodchap_file && prev < start /* &&!last */ && end - start > 2) {
//		fprintf(ipodchap_file,"CHAPTER01=00:00:00.000\nCHAPTER01NAME=1\n");
		fprintf(ipodchap_file, "CHAPTER%.2i=%s\nCHAPTER%.2iNAME=%d\n", i+2,dblSecondsToStrMinutes(((double)end) / fps), i+2, i+2 );
	}
	CLOSEOUTFILE(ipodchap_file);

	if (edlp_file && prev < start /* &&!last */ && end - start > 2) {
		if (start < 5)
			start = 0;
		fprintf(edlp_file, "%.2f\t%.2f\t0\n", (double)start / fps + initial_pts/ 90000.0, (double)end / fps  + initial_pts/ 90000.0);
	}
	CLOSEOUTFILE(edlp_file);

	if (bcf_file && prev < start /* &&!last */ && end - start > 2) {
		fprintf(bcf_file, "1,%.0f,%.0f\n", (double)start * 1000.0 / fps , (double)end * 1000.0 / fps );
	}
	CLOSEOUTFILE(bcf_file);

	if (edlx_file) {
		if (prev < start /* &&!last */ && end - start > 2) {
			fprintf(edlx_file, "<region start=\"%I64d\" end=\"%I64d\"/> \n", frame[start].goppos, frame[end].goppos);
		}
		if (last) {
			fprintf(edlx_file, "</regionlist>\n");
		}
	}
	CLOSEOUTFILE(edlx_file);

	if (womble_file) {
// CLIPLIST: #1 show  
// CLIP: morse.mpg 
// 6 0 9963  
		if (!last) {
			if (start - prev > fps) {
				fprintf(womble_file, "CLIPLIST: #%i show\nCLIP: %s\n6 %li %li\n", i+1, mpegfilename,prev+1, start-prev);
			}
// CLIPLIST: #2 commercial  
// CLIP: morse.mpg  
// 6 9963 5196  

			fprintf(womble_file, "CLIPLIST: #%i commercial\nCLIP: %s\n6 %li %li\n", i+1, mpegfilename, start, end - start);
		} else {
			if (end - prev > 0)
				fprintf(womble_file, "CLIPLIST: #%i show\nCLIP: %s\n6 %li %li\n", i+1, mpegfilename, prev+1, end - prev);
		}
	}
	CLOSEOUTFILE(womble_file);

	if (mls_file) {
		if (i == 0) {
			count = (commercial_count)*2+1;
			if (commercial[commercial_count].end_frame < frame_count-2)
				count += 2;
			if (start < fps)
				count -= 1;
			fprintf(mls_file, "[BookmarkList]\nPathName= %s\nVideoStreamID= 0\nFormat= frame\nCount= %d\n", mpegfilename, count);
			if (start >= fps)
				fprintf(mls_file, "%11i 1\n", 0);
		}
		else
			fprintf(mls_file, "%11li 1\n", prev);
		if (!last)
			fprintf(mls_file, "%11li 0\n", start);
	}
	CLOSEOUTFILE(mls_file);

	if (mpgtx_file) {
		if (!last) {
			if (start - prev > 0) {
				fprintf(mpgtx_file, "[%s-",	(prev < fps ? "":intSecondsToStrMinutes( (int) (prev/fps ))));
				fprintf(mpgtx_file, "%s] ", intSecondsToStrMinutes( (int) (start/fps)));
			}
		} else {
			if (end - prev > 0)
				fprintf(mpgtx_file, "[%s-]",	intSecondsToStrMinutes( (int) ((prev+1)/fps )));
			fprintf(mpgtx_file, "\n");
		}
	}
	CLOSEOUTFILE(mpgtx_file);

	if (dvrcut_file) {
		if (start - prev > (int)fps /* && start > 2*fps */) {
			fprintf(dvrcut_file, "%s ",	intSecondsToStrMinutes( (int) (prev/fps )));
			fprintf(dvrcut_file, "%s ", intSecondsToStrMinutes( (int) (start/fps)));
		}
		if (last) {
			fprintf(dvrcut_file, "\n");
		}
	}
	CLOSEOUTFILE(dvrcut_file);

	if (dvrmstb_file) {
		if (end - start > 1) {
			if (start == 1) start = 0;
			fprintf(dvrmstb_file, "  <commercial start=\"%f\" end=\"%f\" />\n", start/fps, end/fps);
		}
		if (last) {
			fprintf(dvrmstb_file, " </root>\n");
		}
	}
	CLOSEOUTFILE(dvrmstb_file);

	if (mpeg2schnitt_file) {
		if (end - start > 1) {
			fprintf(mpeg2schnitt_file, "/o%ld ",	start);
			fprintf(mpeg2schnitt_file, "/i%ld ", end);
		}
		if (last) {
			fprintf(mpeg2schnitt_file, "\n");
		}
	}
	CLOSEOUTFILE(mpeg2schnitt_file);

	if (cuttermaran_file) {
		if (prev+1 < start) {
			fprintf(cuttermaran_file, "<CutElements refVideoFile=\"0\" StartPosition=\"%li\" EndPosition=\"%li\">\n", prev+1, start-1);
			fprintf(cuttermaran_file, "<CurrentFiles refVideoFiles=\"0\" /> <cutAudioFiles refAudioFile=\"1\" /></CutElements>\n");
		}
		if (last)
		{
			if (cuttermaran_options[0] == 0)
				fprintf(cuttermaran_file, "<CmdArgs OutFile=\"%s_clean.m2v\" cut=\"true\" unattended=\"true\" snapToCutPoints=\"true\" closeApp=\"true\" />\n</StateData>\n",basename);
			else
				fprintf(cuttermaran_file, "<CmdArgs OutFile=\"%s_clean.m2v\" %s />\n</StateData>\n",basename, cuttermaran_options);
		}
	}
	CLOSEOUTFILE(cuttermaran_file);
}


char CompareLetter(int value, int average, int i)
{
	if (cblock[i].reffer == '+' || cblock[i].reffer == '-') 
	{
		if (value > 1.2 * average)
		{
			if (cblock[i].reffer == '-')
				return('=');
			else
				return('!');
		}
		if (value < 0.8 * average)
		{
			if (cblock[i].reffer == '-')
				return('!');
			else
				return('=');
		}
	}
	if (value > average)
	{
		return('+');
	}
	if (value < average)
	{
		return('-');
	}
	return('0');

}

void BuildCommercial()
{
	int i;
	commercial_count = -1;
	i = 0;
	while (i < block_count) {
		if (cblock[i].score > global_threshold 
//			&& 
//			( cblock[i].score >= 100 ||
//			!((commDetectMethod & LOGO) && cblock[i].logo > 0.5 && (cblock[i].f_end - cblock[i].f_start) > (int) (min_show_segment_length*fps) ))
			) {
			commercial_count++;
			commercial[commercial_count].start_frame = cblock[i].f_start/*+ (cblock[i].bframe_count / 2)*/;
			commercial[commercial_count].end_frame = cblock[i].f_end/* + (cblock[i + 1].bframe_count / 2)*/;
			commercial[commercial_count].length = (commercial[commercial_count].end_frame -	commercial[commercial_count].start_frame) /	fps;
			commercial[commercial_count].start_block = i;
			commercial[commercial_count].end_block = i;
			cblock[i].iscommercial = true;
			i++;
			while (i < block_count && cblock[i].score > global_threshold 
//				&& 
//				( cblock[i].score >= 100 ||
//				!((commDetectMethod & LOGO) && cblock[i].logo > 0.5 && (cblock[i].f_end - cblock[i].f_start) > (int) (min_show_segment_length*fps) ))
				) {
				commercial[commercial_count].end_frame = cblock[i].f_end/* + (cblock[i + 1].bframe_count / 2)*/;
				commercial[commercial_count].length = (commercial[commercial_count].end_frame -	commercial[commercial_count].start_frame) /	fps;
				commercial[commercial_count].end_block = i;
				cblock[i].iscommercial = true;
				i++;
			}
		}
		else
			cblock[i].iscommercial = false;
		i++;
	}
}


bool OutputBlocks(void) {
	int		i,k,laststart;
	long	prev;
	double comlength;
	double	threshold;
	bool	lastBlockWasCommercial = false;
	bool	foundCommercials = false;
	bool	deleted = false;
	char	cs[10];

	if (global_threshold >= 0.0) {
		threshold = global_threshold;
	} else {
		threshold = FindScoreThreshold(score_percentile);
	}

	OpenOutputFiles();


	Debug(1, "Threshold used - %.4f", threshold);
	threshold = ceil(threshold * 100) / 100.0;
	Debug(1, "\tAfter rounding - %.4f\n", threshold);

	BuildCommercial();

#ifdef undef
	commercial_count = -1;
	i = 0;
	while (i < block_count) {
		if (cblock[i].score > threshold 
//			&& 
//			( cblock[i].score >= 100 ||
//			!((commDetectMethod & LOGO) && cblock[i].logo > 0.5 && (cblock[i].f_end - cblock[i].f_start) > (int) (min_show_segment_length*fps) ))
			) {
			commercial_count++;
			commercial[commercial_count].start_frame = cblock[i].f_start/*+ (cblock[i].bframe_count / 2)*/;
			commercial[commercial_count].end_frame = cblock[i].f_end/* + (cblock[i + 1].bframe_count / 2)*/;
			commercial[commercial_count].length = (commercial[commercial_count].end_frame -	commercial[commercial_count].start_frame) /	fps;
			commercial[commercial_count].start_block = i;
			commercial[commercial_count].end_block = i;
			cblock[i].iscommercial = true;
			i++;
			while (i < block_count && cblock[i].score > threshold 
//				&& 
//				( cblock[i].score >= 100 ||
//				!((commDetectMethod & LOGO) && cblock[i].logo > 0.5 && (cblock[i].f_end - cblock[i].f_start) > (int) (min_show_segment_length*fps) ))
				) {
				commercial[commercial_count].end_frame = cblock[i].f_end/* + (cblock[i + 1].bframe_count / 2)*/;
				commercial[commercial_count].length = (commercial[commercial_count].end_frame -	commercial[commercial_count].start_frame) /	fps;
				commercial[commercial_count].end_block = i;
				cblock[i].iscommercial = true;
				i++;
			}
		}
		else
			cblock[i].iscommercial = false;
		i++;
	}
#endif

	
	if (!(disable_heuristics & (1 << (5 - 1)))) {
	
	if (delete_block_after_commercial > 0) {
		for (k = commercial_count; k >= 0; k--) {
			i = commercial[k].end_block + 1;
			if (i < block_count && cblock[i].length < delete_block_after_commercial &&
				cblock[i].score < threshold) {
			Debug(3, "H5 Deleting cblock %i because it is short and comes after a commercial.\n",
						i);
				commercial[k].end_frame = cblock[i].f_end/* + (cblock[i + 1].bframe_count / 2)*/;
				commercial[k].length = (commercial[k].end_frame -	commercial[k].start_frame) /	fps;
				commercial[k].end_block = i;
				cblock[i].iscommercial = true;
				cblock[i].cause |= C_H5;
				cblock[i].score = 99.99;
				cblock[i].more |= C_H5;
			}
		}
	}	

	if (commercial_count > -1 && 
		commercial[commercial_count].end_block < block_count - 1 &&
		cblock[block_count-1].f_end - cblock[commercial[commercial_count].end_block].f_end < fps * min_show_segment_length / 2 ) {
		commercial[commercial_count].end_block = block_count-1;		
		commercial[commercial_count].end_frame = cblock[block_count-1].f_end/* + (cblock[i + 1].bframe_count / 2)*/;
		commercial[commercial_count].length = (commercial[commercial_count].end_frame -	commercial[commercial_count].start_frame) /	fps;
		Debug(3, "H5 Deleting cblock %i of %i seconds because it comes after the last commercial and its too short.\n",
						block_count-1, (int)cblock[block_count-1].length);
		cblock[block_count-1].cause |= C_H5;
		cblock[block_count-1].score = 99.99;
		cblock[block_count-1].more |= C_H5;
	}

	if (commercial_count > -1 && 
		commercial[0].start_block == 1 && 
		cblock[0].f_end < min_commercialbreak * fps) {
		commercial[0].start_block = 0;		
		commercial[0].start_frame = cblock[0].f_start/* + (cblock[i + 1].bframe_count / 2)*/;
		commercial[0].length = (commercial[0].end_frame -	commercial[0].start_frame) /	fps;
		Debug(3, "H5 Deleting cblock %i of %i seconds because its too short and before first commercial.\n",
						0, (int)cblock[0].length);
		cblock[0].score = 99.99;
		cblock[0].cause |= C_H5;
		cblock[0].more |= C_H5;

	}

}
	

	Debug(2, "\n\n\t---------------------\n\tInitial Commercial List\n\t---------------------\n");
	for (i = 0; i <= commercial_count; i++) {
		Debug(
			2,
			"%2i) %6i\t%6i\t%s\n",
			i,
			commercial[i].start_frame,
			commercial[i].end_frame,
			dblSecondsToStrMinutes(commercial[i].length)
		);
	}

#if 1




	if (!(disable_heuristics & (1 << (6 - 1)))) {
		
		// Delete too long/short commercials
		for (k = commercial_count; k >= 0; k--) {
			if ( (commercial[k].start_frame > fps   || commercial[k].length < 10.2 /* Sage bug fix */ )  
				&&		// Do not delete too short first or last commercial
				((commercial[k].length > max_commercialbreak && k != 0 && k != commercial_count) || 
				 (commercial[k].length < min_commercialbreak)) &&
				(cblock[block_count-1].f_end - commercial[k].start_frame) > min_commercial_break_at_start_or_end*fps  &&
				(commercial[k].end_frame) > min_commercial_break_at_start_or_end*fps )
			{
				for (i = commercial[k].start_block; i <= commercial[k].end_block; i++) {
					Debug(3, "H6 Deleting block %i because it is part of a too short or too long commercial.\n",
						i);
					cblock[i].score = 0;
					cblock[i].cause |= C_H6;
					cblock[i].less |= C_H6;
				}
				for (i = k; i < commercial_count; i++) {
					commercial[i] = commercial[i + 1];
				}
				commercial_count--;
				deleted = true;
			}
		}
#ifdef NOTDEF
// keep first seconds
		if (always_keep_first_seconds && commercial_count >= 0) {
			k = 0;
			if ( commercial[k].end_frame < fps * always_keep_first_seconds)
			{
				for (i = commercial[k].start_block; i <= commercial[k].end_block; i++) {
					Debug(3, "H6 Deleting block %i because the first %d seconds should always be kept.\n",
						i, always_keep_first_seconds);
					cblock[i].score = 0;
					cblock[i].cause |= C_H6;
					cblock[i].less |= C_H6;
				}
				for (i = k; i < commercial_count; i++) {
					commercial[i] = commercial[i + 1];
				}
				commercial_count--;
				deleted = true;
			}
		}
		if (always_keep_last_seconds && commercial_count >= 0) {
			k = commercial_count;
			if (cblock[block_count-1].f_end - commercial[k].start_frame < fps * always_keep_last_seconds)
			{
				for (i = commercial[k].start_block; i <= commercial[k].end_block; i++) {
					Debug(3, "H6 Deleting block %i because the last %d seconds should always be kept.\n",
						i, always_keep_last_seconds);
					cblock[i].score = 0;
					cblock[i].cause |= C_H6;
					cblock[i].less |= C_H6;
				}
				for (i = k; i < commercial_count; i++) {
					commercial[i] = commercial[i + 1];
				}
				commercial_count--;
				deleted = true;
			}
		}
#endif

/*
		// Delete too short first commercial
		k = 0;
		if (commercial_count >= 0 && commercial[k].start_frame < fps &&
			commercial[k].length < min_commercial_break_at_start_or_end) {
			for (i = commercial[k].start_block; i <= commercial[k].end_block; i++) {
				Debug(3, "H6 Deleting block %i because it is part of a too short commercial at the start of the recording.\n",
					i);
				cblock[i].score = 0;
				cblock[i].cause |= C_H6;
				cblock[i].less |= C_H6;
			}
			for (i = k; i < commercial_count; i++) {
				commercial[i] = commercial[i + 1];
			}
			commercial_count--;
			deleted = true;
		}
		// Delete too short last commercial
		k = commercial_count;
		if (commercial_count >= 0 && (cblock[block_count-1].f_end - commercial[k].end_frame) < fps &&
			commercial[k].length < min_commercial_break_at_start_or_end) {
			for (i = commercial[k].start_block; i <= commercial[k].end_block; i++) {
				Debug(3, "H6 Deleting block %i because it is part of a too short commercial at the end of the recording.\n",
					i);
				cblock[i].score = 0;
				cblock[i].cause |= C_H6;
				cblock[i].less |= C_H6;
			}
			for (i = k; i < commercial_count; i++) {
				commercial[i] = commercial[i + 1];
			}
			commercial_count--;
			deleted = true;
		}
*/
/*
	// Delete too short shows
	for (k = commercial_count-1; k >= 0; k--) {
		if ( commercial[k+1].start_frame - commercial[k].end_frame < min_show_segment_length / 2.5 * fps ||
			 (commercial[k].end_frame > after_start &&
			  commercial[k].end_frame < before_end &&
			  commercial[k+1].start_frame - commercial[k].end_frame < min_show_segment_length  * fps)
			) {
			for (i = commercial[k].end_block+1; i < commercial[k+1].start_block; i++) {
				cblock[i].score = 99.99;
				cblock[i].cause |= C_H6;
				cblock[i].less |= C_H6;
			}
			commercial[k].end_block = commercial[k+1].end_block;
			commercial[k].end_frame = commercial[k+1].end_frame;
			commercial[k].length = (commercial[k].end_frame - commercial[k].start_frame) / fps;

			for (i = k+1; i < commercial_count; i++) {
					commercial[i] = commercial[i + 1];
			}
			commercial_count--;
			deleted = true;
		}
	}
*/

	}
	if (delete_show_after_last_commercial &&
		commercial_count > -1 && 
	//	( commercial[commercial_count].end_block == block_count - 2 || commercial[commercial_count].end_block == block_count - 3) && 
		((delete_show_after_last_commercial == 1 && cblock[commercial[commercial_count].start_block].f_end > before_end) ||
		 (delete_show_after_last_commercial * fps > cblock[block_count-1].f_end - cblock[commercial[commercial_count].start_block].f_start) )

		&&
		commercial[commercial_count].end_block < block_count-1
		) {
		i = commercial[commercial_count].end_block + 1;
		commercial[commercial_count].end_block = block_count-1;		
		commercial[commercial_count].end_frame = cblock[block_count-1].f_end/* + (cblock[i + 1].bframe_count / 2)*/;
		commercial[commercial_count].length = (commercial[commercial_count].end_frame -	commercial[commercial_count].start_frame) /	fps;
		while (i < block_count) {
			Debug(3, "H5 Deleting cblock %i of %i seconds because it comes after the last commercial.\n",
						i, (int)cblock[i].length );
			cblock[i].cause |= C_H5;
			cblock[i].score = 99.99;
			cblock[i].more |= C_H5;
			i++;
		}
	}



	if (delete_show_before_first_commercial &&
		commercial_count > -1 && 
		commercial[0].start_block == 1 && 
		((delete_show_before_first_commercial == 1 && cblock[commercial[0].end_block].f_end < after_start) ||
		(delete_show_before_first_commercial * fps > cblock[commercial[0].end_block].f_end))
		) {
		commercial[0].start_block = 0;		
		commercial[0].start_frame = cblock[0].f_start/* + (cblock[i + 1].bframe_count / 2)*/;
		commercial[0].length = (commercial[0].end_frame -	commercial[0].start_frame) /	fps;
		Debug(3, "H5 Deleting cblock %i of %i seconds because it comes before the first commercial.\n",
						0, (int)cblock[0].length);
		cblock[0].score = 99.99;
		cblock[0].cause |= C_H5;
		cblock[0].more |= C_H5;

	}

// keep first seconds
		if (always_keep_first_seconds && commercial_count >= 0) {
			k = 0;
			while (commercial[k].end_frame < fps * always_keep_first_seconds)
			{
				Debug(3, "Deleting commercial block %i because the first %d seconds should always be kept.\n",
						k, always_keep_first_seconds);
				for (i = k; i < commercial_count; i++) {
					commercial[i] = commercial[i + 1];
				}
				commercial_count--;
				deleted = true;
			}
			if (commercial[k].start_frame < fps * always_keep_first_seconds)
			{
				Debug(3, "Shortening commercial block %i because the first %d seconds should always be kept.\n",
						k, always_keep_first_seconds);
				commercial[k].start_frame = fps * always_keep_first_seconds;
			}
		}
		if (always_keep_last_seconds && commercial_count >= 0) {
			k = commercial_count;
			while (cblock[block_count-1].f_end - commercial[k].start_frame < fps * always_keep_last_seconds)
			{
				Debug(3, "Deleting commercial block %i because the last %d seconds should always be kept.\n",
						k, always_keep_last_seconds);
				commercial_count--;
				k = commercial_count;
				deleted = true;
			}
			if (cblock[block_count-1].f_end - commercial[k].end_frame < fps * always_keep_last_seconds)
			{
				Debug(3, "Shortening commercial block %i because the last %d seconds should always be kept.\n",
						k, always_keep_last_seconds);
				commercial[k].end_frame = cblock[block_count-1].f_end - fps * always_keep_last_seconds;
			}
		}



	if (deleted)
		Debug(1, "\n\n\t---------------------\n\tFinal Commercial List\n\t---------------------\n");
	else
		Debug(1, "No change\n");
#endif


	// Apply padding
	for (i = 0; i <= commercial_count; i++) {
		commercial[i].start_frame += padding*fps - remove_before*fps;
		if (i != commercial_count) commercial[i].end_frame -= padding*fps - remove_after*fps;
		commercial[i].length += -2*padding + remove_before + remove_after;
	}



	comlength = 0.;
	for (i = 0; i < commercial_count; i++) {
		comlength += commercial[i].length;
	}
//	Debug(1, "Total commercial length found: %s\n",	dblSecondsToStrMinutes(comlength));

	if ((zoomplayer_chapter_file) &&
//		(commercial[0].length >= min_commercialbreak) &&
//		(commercial[0].length <= max_commercialbreak) &&
		(commercial[0].start_frame > 5)) {
		fprintf(zoomplayer_chapter_file, "AddChapter(1,Show Segment)\n");
	}

	
	prev = -1;
	for (i = 0; i <= commercial_count; i++) {
//		if ((commercial[i].length >= min_commercialbreak) && (commercial[i].length <= max_commercialbreak)) 
		{
			foundCommercials = true;
			if (deleted)
			Debug(
				1,
				"%i - start: %6i\tend: %6i\t[%6i:%6i]\tlength: %s\n",
				i + 1,
				commercial[i].start_frame,
				commercial[i].end_frame,
				commercial[i].start_block,
				commercial[i].end_block,
				dblSecondsToStrMinutes(commercial[i].length)
			);
			OutputCommercialBlock(i, prev, commercial[i].start_frame, commercial[i].end_frame, (commercial[i].end_frame < frame_count-2 ? false : true));
			prev = commercial[i].end_frame;
		}
	}

	if (commercial[commercial_count].end_frame < frame_count-2)
		OutputCommercialBlock(commercial_count+1, prev, frame_count-2, frame_count-1, true);

	if (output_videoredo) {
		sprintf(filename, "%s.VPrj", basename);
		videoredo_file = fopen(filename, "a+");
		if (videoredo_file) {
			for (i = 0; i < block_count; i++) { 
				fprintf(videoredo_file, "<SceneMarker %d>%.0f\n", i, max(cblock[i].f_end-videoredo_offset-1,0) / fps * 10000000);
			}
			fclose(videoredo_file);
		}
	}

	if (output_chapters) {
//		sprintf(filename, "%s.chap", basename);
//		chapters_file = fopen(filename, "a+");
		if (chapters_file) {
			for (i = 0; i < block_count; i++) { 
				fprintf(chapters_file, "%d\n", cblock[i].f_end);
			}
			fclose(chapters_file);
		}
	}

	reffer_count = commercial_count;
	for (i = 0; i <= commercial_count; i++) {
		reffer[i].start_frame = commercial[i].start_frame;
		reffer[i].end_frame = commercial[i].end_frame;
	}

	InputReffer(".ref");

	if (output_tuning) {
		sprintf(filename, "%s.tun", basename);
		tuning_file = fopen(filename, "w");
		fprintf(tuning_file,"max_volume=%6i\n", min_volume+200);
		fprintf(tuning_file,"max_avg_brightness=%6i\n", min_brightness_found+5);
		fprintf(tuning_file,"max_commercialbreak=%6i\n", max_logo_gap+10);
		fprintf(tuning_file,"shrink_logo=%6i\n", logo_overshoot);
		fprintf(tuning_file,"min_show_segment_length=%6i\n", max_nonlogo_block_length+10);
		fprintf(tuning_file,"logo_threshold=%.3f\n", logo_quality);
	}




	if (verbose) {
		Debug(1, "\nLogo fraction:              %.4f      %s\n",logoPercentage, ((commDetectMethod & LOGO) ? (reverseLogoLogic? "(Reversed Logo Logic)": "") : "Logo disabled") );
		Debug(1,   "Maximum volume found:       %6i\n", maxi_volume);
		Debug(1,   "Average volume:             %6i\n", avg_volume);
		Debug(1,   "Sound threshold:            %6i\n", max_volume);
		Debug(1,   "Silence threshold:          %6i\n", max_silence);
		Debug(1,   "Minimum volume found:       %6i\n", min_volume);
		Debug(1,   "Average frames with silence:%6i\n", avg_silence);
		Debug(1,   "Black threshold:            %6i\n", max_avg_brightness);
		Debug(1,   "Minimum brightness found:   %6i\n", min_brightness_found);
		Debug(1,   "Minimum bright pixels found:%6i\n", min_hasBright);
		Debug(1,   "Minimum dim level found:    %6i\n", min_dimCount);
		Debug(1,   "Average brightness:         %6i\n", avg_brightness);
		Debug(1,   "Uniformity level:           %6i\n", non_uniformity);
		Debug(1,   "Average non uniformity:     %6i\n", avg_uniform);
		Debug(1,   "Maximum gap between logo's: %6i\n", max_logo_gap);
		Debug(1,   "Suggested logo_threshold:   %.4f\n",logo_quality);
		Debug(1,   "Suggested shrink_logo:	    %6i\n", logo_overshoot);
		Debug(1,   "Max commercial size found:  %6i\n", max_nonlogo_block_length);
		Debug(1,   "Dominant aspect ratio:      %.4f\n",dominant_ar);
		Debug(1,   "Score threshold:            %.4f\n", threshold);
		Debug(1,   "Framerate:                  %2.2f\n", fps);

		Debug(1,   "Total commercial length:    %s\n",	dblSecondsToStrMinutes(comlength));
		Debug(1,   "Block list after weighing\n----------------------------------------------------\n", threshold);
		Debug(
				1,
				"  #     sbf  bs  be     fs     fe    sc      len   scr cmb   ar                   cut bri  logo   vol  sil corr stdev        cc\n"
		);

//		if (output_training) {
//			fprintf(training_file, TRAINING_LAYOUT,
//				"0", 0, 0, 0, 0,0,0, 0, 0, 0, 0);
//		}
			
		
		
		
		for (i = 0; i < block_count; i++) {
/*
			cs[5] = (cblock[i].cause & 16 ? 'b' : ' ');
			cs[4] = (cblock[i].cause & 8  ? 'u' : ' ');
			cs[3] = (cblock[i].cause & 32 ? 'a' : ' ');
			cs[2] = (cblock[i].cause & 4  ? 's' : ' ');
			cs[1] = (cblock[i].cause & 1  ? 'l' : ' ');
			cs[0] = (cblock[i].cause & 2  ? 'c' : ' ');
			cs[6] = 0;
*/
			
			Debug(
				1,
				"%3i:%c%c %4i %3i %3i %6i %6i %6.2f %8.3f %5.2f %3i %4.2f %s %4i%c %4.2f %4i%c %2i%c %6.3f %5i %-10s",
				i,
				CheckFramesForCommercial(cblock[i].f_start+cblock[i].b_head,cblock[i].f_end - cblock[i].b_tail),
				CheckFramesForReffer(cblock[i].f_start+cblock[i].b_head,cblock[i].f_end - cblock[i].b_tail),
				cblock[i].bframe_count,
				cblock[i].b_head,
				cblock[i].b_tail,
				cblock[i].f_start,
				cblock[i].f_end,
				cblock[i].score,
				cblock[i].length,
//				cblock[i].schange_count,
				cblock[i].schange_rate,
				cblock[i].combined_count,
				cblock[i].ar_ratio,
				CauseString(cblock[i].cause),
				cblock[i].brightness,
				CompareLetter(cblock[i].brightness,avg_brightness,i),
				cblock[i].logo,
				cblock[i].volume,
				CompareLetter(cblock[i].volume,avg_volume,i),
				cblock[i].silence,
				CompareLetter(cblock[i].silence,avg_silence,i),
				0.0 /*cblock[i].correlation */ ,
				cblock[i].stdev,
				CCTypeToStr(cblock[i].cc_type)
			);
			if (commDetectMethod & LOGO) {
//				if (CheckFramesForLogo(cblock[i].f_start, cblock[i].f_end)) {
//					Debug(1, "\tLogo Present\n");
//				} else {
					Debug(1, "\n");
//				}
			} else {
				Debug(1, "\n");
			}
		}

		OutputAspect();
		OutputTraining();

		
		
//		if (output_training) {
//			fprintf(training_file, TRAINING_LAYOUT,
//				"0", 0, 0, 100, 0,0,0, 0, 0, 0, 0);
//		}

	}	

//	OutputCleanMpg();
//	OutputDebugWindow(false,0);
	return (foundCommercials);
}

void OutputStrict(double len, double delta, double tol)
{
return;
	if (output_training && !training_file) {
		training_file = fopen("comskip.csv", "a+");
//		fprintf(training_file, "// score, length, fraction, position,combined, ar error, logo, strict \n");
	}
	if (training_file) 
		fprintf(training_file, "%+f,%+f,%+f, %s\n", len,delta, tol, basename);
}




void OutputTraining() {
	int i,s,e,r;
//	return;
	if (!output_training)
		return;
	training_file = fopen("comskip.csv", "a+");

#ifdef WRITEPATTERN
	r = (reffer[0].start_frame/fps < 30.0 ? reffer_count: reffer_count+1);
	if (reffer[0].start_frame/fps < 30.0)
		s = reffer[0].end_frame;
	else
		s = 0;
	fprintf(training_file, "\"%s\",%f,%d,", basename,  (reffer[reffer_count].start_frame - s)/fps, r);
	for (i = 0; i < 40; i++)
	{
		if (i <= reffer_count) {
			if (i == 0) 
				e = 0;
			else
				e = reffer[i-1].end_frame;
			if (i == reffer_count)
				s = 0;
			else
				s = (reffer[i].end_frame - reffer[i].start_frame);
			if (i > 0)
				fprintf(training_file, "%f,%f,", (reffer[i].start_frame-e)/fps, s/fps);
			else 
			{
				if (reffer[i].start_frame/fps > 30.0)
					fprintf(training_file, "%f,%f, %f,%f,", 0.0, 0.0, (reffer[i].start_frame-e)/fps,s/fps);
				else
					fprintf(training_file, "%f,%f,", (reffer[i].start_frame-e)/fps,s/fps);
			}
		} else {
			fprintf(training_file, "%f,%f,", 0.0, 0.0);
		}
	}
	fprintf(training_file, "0\n", basename);

	
	r = (commercial[0].start_frame/fps < 30.0 ? commercial_count: commercial_count+1);
	if (commercial[0].start_frame/fps < 30.0)
		s = commercial[0].end_frame;
	else
		s = 0;
	fprintf(training_file, "\"%s\",%f,%d,", basename,  (commercial[commercial_count].start_frame - s)/fps, r);
	for (i = 0; i < 40; i++)
	{
		if (i <= commercial_count) {
			if (i == 0) 
				e = 0;
			else
				e = commercial[i-1].end_frame;
			if (i == commercial_count)
				s = 0;
			else
				s = (commercial[i].end_frame - commercial[i].start_frame);
			if (i > 0)
				fprintf(training_file, "%f,%f,", (commercial[i].start_frame-e)/fps, s/fps);
			else 
			{
				if (commercial[i].start_frame/fps > 30.0)
					fprintf(training_file, "%f,%f, %f,%f,", 0.0, 0.0, (commercial[i].start_frame-e)/fps,s/fps);
				else
					fprintf(training_file, "%f,%f,", (commercial[i].start_frame-e)/fps,s/fps);
			}
		} else {
			fprintf(training_file, "%f,%f,", 0.0, 0.0);
		}
	}
	fprintf(training_file, "0\n", basename);

#else

#define TRAINING_LAYOUT	"%3d,%c,%c,%7.2f,%7.2f,%7.2f,%7.2f,%7.2f,%5.2f,%5.2f,\"%10s\",\"%10s\",\"%10s\",\"%s\"\n"

	fprintf(training_file, "block, cm,rf, score, length, start, end, fromend ar, logo, cause, less, more\n");

	for (i = 0; i < block_count; i++) {
		if (output_training) {
			fprintf(training_file, TRAINING_LAYOUT,
				i,
				CheckFramesForCommercial(cblock[i].f_start+cblock[i].b_head,cblock[i].f_end - cblock[i].b_tail),
				CheckFramesForReffer(cblock[i].f_start+cblock[i].b_head,cblock[i].f_end - cblock[i].b_tail),
				cblock[i].score,
				cblock[i].length,
				cblock[i].f_start/fps,
				cblock[i].f_end/fps,
				(cblock[block_count-1].f_end - cblock[i].f_end)/fps,
				cblock[i].ar_ratio,
				cblock[i].logo,
				CauseString(cblock[i].cause),
				CauseString(cblock[i].less),
				CauseString(cblock[i].more),
				basename,
				"a","b");
					
		}
	}
#endif
  
}


static unsigned char MPEG2SysHdr[] = {0x00, 0x00, 0x01, 0xBB, 00, 0x12, 0x80, 0x8E, 0xD3, 0x04, 0xE1, 0x7F, 0xB9, 0xE0, 0xE0, 0xB8, 0xC0, 0x54, 0xBD, 0xE0, 0x3A, 0xBF, 0xE0, 0x02};

bool OutputCleanMpg()
{

  FILE *infile=0;
	int inf, outf;
	int i,j,c;
	__int64 startpos=0, endpos=0, begin=0;
	int len;
	int prevperc,curperc;
	char *Buf;//[65536];
	
	bool firstbl = true;
#define BufSize 1<<22
	
	long dwPackStart;

	if (outputfilename[0] == 0) return(true);

	if (!(Buf=(char*)malloc(BufSize))) return(false);
	
	dwPackStart=0xBA010000;
	
#ifdef WIN32
	outf = _creat(outputfilename, _S_IREAD | _S_IWRITE);
	if(outf<0) return(false);
	inf = _open(mpegfilename, _O_RDONLY | _O_BINARY);
#else
  outf = open(outputfilename, O_CREAT | O_TRUNC | O_WRONLY, S_IRUSR | S_IWUSR);
	if(outf<0) 
    return(false);

  infile=fopen(mpegfilename,"rb");
  inf = fileno(infile);
#endif
	
/*	
	if (_lseeki64(Infile[File_Limit-1], process.leftlba*BUFFER_SIZE,SEEK_SET)!= -1L)
	{
		
		j = _read(Infile[File_Limit-1], Buf, BufSize);
		if (j>=BUFFER_SIZE)
		{
			for(i=0; i<(j-4); i++)
			{
				if(*((UNALIGNED DWORD*)(Buf+i)) == dwPackStart)
				{
					startpos = (process.leftlba*BUFFER_SIZE) + i;
					endpos = process.total;
					
					if (_lseeki64(Infile[File_Limit-1], process.rightlba*BUFFER_SIZE,SEEK_SET)!= -1L)
					{
						j = _read(Infile[File_Limit-1], Buf, BufSize);
						if (j>=BUFFER_SIZE)
						{
							for(i=0; i<(j-4); i++)
							{
								if(*((UNALIGNED DWORD*)(Buf+i)) == dwPackStart)
								{
									endpos = (process.rightlba*BUFFER_SIZE) + i;
									break;
								}
							}
							
						}
						
					}
					
					*/
	
	startpos = frame[1].goppos;

	for (c=0; c<=commercial_count; c++) {

		endpos = frame[commercial[c].start_frame].goppos;
#ifdef _WIN32
		_lseeki64(inf, startpos,SEEK_SET);
#else
		fseeko(infile, startpos,SEEK_SET);
#endif
		
		begin = startpos;
		prevperc = 0;
		
		while (startpos<endpos)
		{
			len = (int)endpos-startpos;
			if(len>BufSize) len = BufSize;//sizeof(Buf);
			i = _read(inf, Buf, (unsigned int)len);
			if(i<=0)
			{
				//				MessageBox(hWnd, "Source read error.         ", "Oops...", MB_ICONSTOP | MB_OK);
				return(false);
			}
			j=0;
			
			if (firstbl)
			{
				firstbl=false;
				j = 14 + (Buf[13] & 7);
        if (*((UNALIGNED DWORD*)(Buf+j)) == 0xBB010000) j=0; //FIXME: unaligned dword may not be the right thing on mac: #defined  away
				else
				{
					_write(outf, Buf, j);
					_write(outf, MPEG2SysHdr, sizeof(MPEG2SysHdr));
				}
			}
			
			if(_write(outf, Buf+j, i-j)<=0)
			{
				//				MessageBox(hWnd, "Write error.         ", "Oops...", MB_ICONSTOP | MB_OK);
				return(false);
			}
			
			if (i!=len) 
			{
				//				MessageBox(hWnd, "Something strange happened. Aborting.         ", "Oops...", MB_ICONSTOP | MB_OK);
				return(false);
			}
			startpos +=len;
			
			curperc = (int)(((startpos-begin)*100)/(endpos-begin));
			
			if (curperc != prevperc)
			{
				//				SendMessage(hBar, PBM_SETPOS, DWORD(curperc),0);
				prevperc=curperc;
			}
			
		}
		startpos = frame[commercial[c].end_frame].goppos;
		
	}
	_close(outf);
	free(Buf);
	return(true);	
}

bool LengthWithinTolerance(double test_length, double expected_length, double tolerance) {
	return (abs((int)(test_length * fps) - (int)(expected_length * fps)) <= (int)(tolerance * fps));
}

bool IsStandardCommercialLength(double length, double tolerance, bool strict) {
	int		i;
	double	local_tolerance;
	int		length_count;
	double	delta;
	int		standard_length[] = { 10, 15, 20, 25, 30, /* 36, */ 45, 60, 90, 120, 150, 180,  5, 35, 40, 50};
	if (strict) {
		length_count = 11;
	} else {
		length_count = 15;
	}

	if (div5_tolerance >= 0) {
		local_tolerance = div5_tolerance;
	} else {
		local_tolerance = tolerance;
	}

	if (local_tolerance < 0.5)
		local_tolerance = 0.5;

	if (local_tolerance > 1.0)
		local_tolerance = 1.0;
	
	length -= 0.020;		// Correction for standard error
	
//	length -= 0.1;		// Correction for standard error

	for (i = 0; i < length_count; i++) {
		if ( standard_length[i] < min_show_segment_length - 3)
			if (LengthWithinTolerance(length, standard_length[i], local_tolerance)) {
				delta = length - standard_length[i];
				OutputStrict(length, delta, local_tolerance);
				return (true);
			}
	}

	return (false);
}

double FindNumber(char* str1, char* str2, double v) {
	boolean negative=false;
	char  tmp[255];
	int i;
	double res = -1;
	if (str1 == 0) {
		return (-1);
	}

  if ((str1 = strstr(str1, str2))) {
		str1 += strlen(str2);
		while (isspace(*str1)) {
			str1++;
		}
		if (*str1 == '-') {
			str1++;
			negative = true;
		}
		res = (negative?-atof(str1):atof(str1));
		sprintf(tmp, "%s%0f\n", str2, res);
	}
	else {
		sprintf(tmp, "%s%0f\n", str2, v);
	}
	i = strlen(tmp);
	while (i >= 2 && tmp[i-2] == '0') {
		tmp[i - 1] = 0;
		tmp[(i--) - 2] = '\n';
	}
	if (i >= 2 && tmp[i-2] == '.') {
		tmp[i - 1] = 0;
		tmp[(i--) - 2] = '\n';
	}
	AddIniString(tmp);
	return (res);
}

char * FindString(char* str1, char* str2, char *v) {
	static char foundText[1024];
	char  tmp[255];
	char *t;
	boolean negative=false;
	double res;
	int found = 0;
	if (str1 == 0) {
		return (0);
	}

  if ((str1 = strstr(str1, str2))) {
		str1 += strlen(str2);
		while (isspace(*str1)) {
			str1++;
		}
	
		if (*str1 == '"') {
			t = foundText;
			str1++;
			while (*str1 != '"' && *str1 != 0 && *str1 != 10) {
				if (*str1 == '\\') {
					*str1++;
					if (*str1 == 'n')
						*str1 = '\n';
	
				}
				*t++ = *str1++;

			}
			*t++ = 0;
			v = foundText;
			found = 1;
			sprintf(tmp, "%s\"%s\"\n", str2, foundText);
//			strcat(ini_text, tmp);
//			return(foundText);
		}
	}

	t = tmp;
	while (*str2)
		*t++ = *str2++;
	*t++ = '"';
	while (*v) {
		switch(*v) {
		case '"':
		case '\\':
			*(t++) = '\\';
			*t++ = *v++;
			break;
		case '\n':
			*t++ = '\\';
			*t++ = 'n';
			v++;
			break;
		case '\t':
			*t++ = '\\';
			*t++ = 't';
			v++;
			break;
		default:
			*(t++) = *v++;
		}
	}
	*t++ = '"';
	*t++ = '\n';
	*t++ = 0;
//	sprintf(tmp, "%s\"%s\"\n", str2, v);
	AddIniString(tmp);
	if (found)
		return(foundText);
	else
		return (0);
}

void AddIniString( char *s)
{
	strcat(ini_text, s);
//	printf("ini = %d\n", strlen(ini_text));
}

char* intSecondsToStrMinutes(int seconds) {
	int minutes, hours;
	hours = (int)(seconds / 3600);
	seconds -= hours * 60 * 60;
	minutes = (int)(seconds / 60);
	seconds -= minutes * 60;
	sprintf(tempString, "%i:%.2i:%.2i", hours, minutes, seconds);
	return (tempString);
}

char* dblSecondsToStrMinutes(double seconds) {
	int minutes, hours;
	hours = (int)(seconds / 3600);
	seconds -= hours * 60 * 60;
	minutes = (int)(seconds / 60);
	seconds -= minutes * 60;
	sprintf(tempString, "%0i:%.2i:%.2d.%.2d", hours, minutes, (int)seconds, (int)((seconds - (int)(seconds))*100) );

	return (tempString);
}

void LoadIniFile() {
//	FILE*				ini_file = NULL;
	char				data[20000];
	char				tempstr[MAX_PATH];
	char*				ts;
	size_t				len = 0;
	double				tmp;
	ini_text[0] = 0;
	//	ini_file = fopen(inifilename, "r");
	if (!ini_file) {
		printf("No INI file found in current directory.  Searching PATH...\n");
		FindIniFile();
		if (*inifilename != '\0') {
			printf("INI file found at %s\n", inifilename);
			ini_file = fopen(inifilename, "r");
		} else {
			printf("No INI file found in PATH...\n");
		}
	}

	if (ini_file) {
		printf("Using %s for initiation values.\n", inifilename);
		len = fread(data, 1, 19999, ini_file);
		fclose(ini_file);
		data[len] = '\0';
		ini_text[0] = 0;
		AddIniString("[Main Settings]\n");
		AddIniString(";the sum of the values for which kind of frames comskip will consider as possible cutpoints: 1=uniform (black or any other color) frame, 2=logo, 4=scene change, 8=resolution change, 16=closed captions, 32=aspect ration, 64=silence, 255=all.\n");
		if ((tmp = FindNumber(data, "detect_method=", (double) commDetectMethod)) > -1) commDetectMethod = (int)tmp;
		AddIniString(";Set to 10 to show a lot of extra info, level 5 is also OK, set to 0 to disable\n");
		if ((tmp = FindNumber(data, "verbose=", (double) verbose)) > -1) verbose = (int)tmp;
		AddIniString(";Frame not black if any of the pixels of the frame has a brightness greater than this (scale 0 to 255)\n");
		if ((tmp = FindNumber(data, "max_brightness=", (double) max_brightness)) > -1) max_brightness = (int)tmp;
		if ((tmp = FindNumber(data, "maxbright=", (double) maxbright)) > -1) maxbright = (int)tmp;
		AddIniString(";Frame not pure black if a small number of the pixels of the frame has a brightness greater than this. To decide if the frame is truly black, comskip will also check average brightness (scale 0 to 255)\n");
		if ((tmp = FindNumber(data, "test_brightness=", (double) test_brightness)) > -1) test_brightness = (int)tmp;
		AddIniString(";\n");
		if ((tmp = FindNumber(data, "max_avg_brightness=", (double) max_avg_brightness)) > -1) max_avg_brightness = (int)tmp;
		AddIniString(";\n");
		if ((tmp = FindNumber(data, "max_commercialbreak=", (double) max_commercialbreak)) > -1) max_commercialbreak = (int)tmp;
		AddIniString(";\n");
		if ((tmp = FindNumber(data, "min_commercialbreak=", (double) min_commercialbreak)) > -1) min_commercialbreak = (int)tmp;
		AddIniString(";\n");
		if ((tmp = FindNumber(data, "max_commercial_size=", (double) max_commercial_size)) > -1) max_commercial_size = (int)tmp;
		AddIniString(";\n");
		if ((tmp = FindNumber(data, "min_commercial_size=", (double) min_commercial_size)) > -1) min_commercial_size = (int)tmp;
		AddIniString(";\n");
		if ((tmp = FindNumber(data, "min_show_segment_length=", (double) min_show_segment_length)) > -1) min_show_segment_length = (double)tmp;
		AddIniString(";\n");
		if ((tmp = FindNumber(data, "max_volume=", (double) max_volume)) > -1) max_volume = (int)tmp;
		AddIniString(";\n");
		if ((tmp = FindNumber(data, "non_uniformity=", (double) non_uniformity)) > -1) non_uniformity = (int)tmp;
		AddIniString("[Detailed Settings]\n");
		if ((tmp = FindNumber(data, "min_silence=", (double) min_silence)) > -1) min_silence = (int)tmp;
		if ((tmp = FindNumber(data, "noise_level=", (double) noise_level)) > -1) noise_level = (int)tmp;
		if ((tmp = FindNumber(data, "brightness_jump=", (double) brightness_jump)) > -1) brightness_jump = (bool) tmp;
		if ((tmp = FindNumber(data, "fps=", (double) fps)) > -1) fps = tmp;
		if ((tmp = FindNumber(data, "validate_silence=", (double) validate_silence)) > -1) validate_silence = (int)tmp;
		if ((tmp = FindNumber(data, "validate_uniform=", (double) validate_uniform)) > -1) validate_uniform = (int)tmp;
		if ((tmp = FindNumber(data, "validate_scenechange=", (double) validate_scenechange)) > -1) validate_scenechange = (int)tmp;
		if ((tmp = FindNumber(data, "global_threshold=", (double) global_threshold)) > -1) global_threshold = (double)tmp;
		if ((tmp = FindNumber(data, "disable_heuristics=", (double) disable_heuristics)) > -1) disable_heuristics = (int)tmp;
		AddIniString("[CPU Load Reduction]\n");
		if ((tmp = FindNumber(data, "play_nice_start=", (double) play_nice_start)) > -1) play_nice_start = (int)tmp;
		if ((tmp = FindNumber(data, "play_nice_end=", (double) play_nice_end)) > -1) play_nice_end = (int)tmp;
		if ((tmp = FindNumber(data, "play_nice_sleep=", (double) play_nice_sleep)) > -1) play_nice_sleep = (long)tmp;
		AddIniString("[Input Correction]\n");
		if ((tmp = FindNumber(data, "max_repair_size=", (double) max_repair_size)) > -1) max_repair_size = (int)tmp;
		if ((tmp = FindNumber(data, "ms_audio_delay=", (double) ms_audio_delay)) > -1) ms_audio_delay = -(int)tmp;
		if ((tmp = FindNumber(data, "volume_slip=", (double) volume_slip)) > -1) volume_slip = (int)tmp;
		if ((tmp = FindNumber(data, "variable_bitrate=", (double) variable_bitrate)) > -1) variable_bitrate = (int)tmp;
#ifdef DONATORS
		if ((tmp = FindNumber(data, "skip_b_frames=", (double) skip_B_frames)) > -1) skip_B_frames = (int)tmp;
		if (skip_B_frames != 0 && max_repair_size == 0) max_repair_size = 40;
#else

#ifdef _DEBUG
		if ((tmp = FindNumber(data, "skip_b_frames=", (double) skip_B_frames)) > -1) skip_B_frames = (int)tmp;
		if (skip_B_frames != 0 && max_repair_size == 0) max_repair_size = 40;
#endif
#endif


		AddIniString("[Aspect Ratio]\n");
		if ((tmp = FindNumber(data, "ar_delta=", (double) ar_delta)) > -1) ar_delta = (double)tmp;
		if ((tmp = FindNumber(data, "cut_on_ar_change=", (double) cut_on_ar_change)) > -1) cut_on_ar_change = (int)tmp;
		AddIniString("[Global Removes]\n");
		if ((tmp = FindNumber(data, "padding=", (double) padding)) > -1) padding = (int)tmp;
		if ((tmp = FindNumber(data, "remove_before=", (double) remove_before)) > -1) remove_before = (int)tmp;
		if ((tmp = FindNumber(data, "remove_after=", (double) remove_after)) > -1) remove_after = (int)tmp;
		if ((tmp = FindNumber(data, "added_recording=", (double) added_recording)) > -1) added_recording = (int)tmp;
		if ((tmp = FindNumber(data, "delete_show_after_last_commercial=", (double) delete_show_after_last_commercial)) > -1) delete_show_after_last_commercial = (int)tmp;
		if ((tmp = FindNumber(data, "delete_show_before_first_commercial=", (double) delete_show_before_first_commercial)) > -1) delete_show_before_first_commercial = (int)tmp;
		if ((tmp = FindNumber(data, "delete_show_before_or_after_current=", (double) delete_show_before_or_after_current)) > -1) delete_show_before_or_after_current = (int)tmp;
		if ((tmp = FindNumber(data, "delete_block_after_commercial=", (double) delete_block_after_commercial)) > -1) delete_block_after_commercial = (int)tmp;
		if ((tmp = FindNumber(data, "min_commercial_break_at_start_or_end=", (double) min_commercial_break_at_start_or_end)) > -1) min_commercial_break_at_start_or_end = (int)tmp;
		if ((tmp = FindNumber(data, "always_keep_first_seconds=", (double) always_keep_first_seconds)) > -1) always_keep_first_seconds = (int)tmp;
		if ((tmp = FindNumber(data, "always_keep_last_seconds=", (double) always_keep_last_seconds)) > -1) always_keep_last_seconds = (int)tmp;
		AddIniString("[USA Specific]\n");
		if ((tmp = FindNumber(data, "intelligent_brightness=", (double) intelligent_brightness)) > -1) intelligent_brightness = (bool) tmp;
		if ((tmp = FindNumber(data, "black_percentile=", (double) black_percentile)) > -1) black_percentile = (double)tmp;
		if ((tmp = FindNumber(data, "uniform_percentile=", (double) uniform_percentile)) > -1) uniform_percentile = (double)tmp;
		if ((tmp = FindNumber(data, "score_percentile=", (double) score_percentile)) > -1) score_percentile = (double)tmp;
		AddIniString("[Main Scoring]\n");
		if ((tmp = FindNumber(data, "length_strict_modifier=", (double) length_strict_modifier)) > -1) length_strict_modifier = (double)tmp;
		if ((tmp = FindNumber(data, "length_nonstrict_modifier=", (double) length_nonstrict_modifier)) > -1) length_nonstrict_modifier = (double)tmp;
		if ((tmp = FindNumber(data, "combined_length_strict_modifier=", (double) combined_length_strict_modifier)) > -1) combined_length_strict_modifier = (double)tmp;
		if ((tmp = FindNumber(data, "combined_length_nonstrict_modifier=", (double) combined_length_nonstrict_modifier)) > -1) combined_length_nonstrict_modifier = (double)tmp;
		if ((tmp = FindNumber(data, "ar_wrong_modifier=", (double) ar_wrong_modifier)) > -1) ar_wrong_modifier = (double)tmp;
		if ((tmp = FindNumber(data, "excessive_length_modifier=", (double) excessive_length_modifier)) > -1) excessive_length_modifier = (double)tmp;
		if ((tmp = FindNumber(data, "dark_block_modifier=", (double) dark_block_modifier)) > -1) dark_block_modifier = (double)tmp;
		if ((tmp = FindNumber(data, "min_schange_modifier=", (double) min_schange_modifier)) > -1) min_schange_modifier = (double)tmp;
		if ((tmp = FindNumber(data, "max_schange_modifier=", (double) max_schange_modifier)) > -1) max_schange_modifier = (double)tmp;
		if ((tmp = FindNumber(data, "logo_present_modifier=", (double) logo_present_modifier)) > -1) logo_present_modifier = (double)tmp;
		if ((tmp = FindNumber(data, "punish_no_logo=", (double) punish_no_logo)) > -1) punish_no_logo = (int)tmp;

		AddIniString("[Detailed Scoring]\n");
		if ((tmp = FindNumber(data, "punish=", (double) punish)) > -1) punish = (int)tmp;
		if ((tmp = FindNumber(data, "reward=", (double) reward)) > -1) reward = (int)tmp;
		if ((tmp = FindNumber(data, "punish_threshold=", (double) punish_threshold)) > -1) punish_threshold = (double)tmp;
		if ((tmp = FindNumber(data, "punish_modifier=", (double) punish_modifier)) > -1) punish_modifier = (double)tmp;
		if ((tmp = FindNumber(data, "reward_modifier=", (double) reward_modifier)) > -1) reward_modifier = (double)tmp;
		AddIniString("[Logo Finding]\n");
		if ((tmp = FindNumber(data, "border=", (double) border)) > -1) border = (int)tmp;
		if ((tmp = FindNumber(data, "give_up_logo_search=", (double) giveUpOnLogoSearch)) > -1) giveUpOnLogoSearch = (int)tmp;
		if ((tmp = FindNumber(data, "delay_logo_search=", (double) delay_logo_search)) > -1) delay_logo_search = (int)tmp;
		if ((tmp = FindNumber(data, "logo_max_percentage_of_screen=", (double) logo_max_percentage_of_screen)) > -1) logo_max_percentage_of_screen = (double)tmp;
		if ((tmp = FindNumber(data, "ticker_tape=", (double) ticker_tape)) > -1) ticker_tape = (int)tmp;
		if ((tmp = FindNumber(data, "ignore_side=", (double) ignore_side)) > -1) ignore_side = (int)tmp;
		if ((tmp = FindNumber(data, "subtitles=", (double) subtitles)) > -1) subtitles = (int)tmp;
		if ((tmp = FindNumber(data, "logo_at_bottom=", (double) logo_at_bottom)) > -1) logo_at_bottom = (int)tmp;
		if ((tmp = FindNumber(data, "logo_threshold=", (double) logo_threshold)) > -1) logo_threshold = (double)tmp;
		if ((tmp = FindNumber(data, "logo_filter=", (double) logo_filter)) > -1) logo_filter = (int)tmp;
		if ((tmp = FindNumber(data, "aggressive_logo_rejection=", (double) aggressive_logo_rejection)) > -1) aggressive_logo_rejection = (bool)tmp;
		if ((tmp = FindNumber(data, "edge_level_threshold=", (double) edge_level_threshold)) > -1) edge_level_threshold = (int)tmp;
		if ((tmp = FindNumber(data, "edge_radius=", (double) edge_radius)) > -1) edge_radius = (int)tmp;
		if ((tmp = FindNumber(data, "edge_weight=", (double) edge_weight)) > -1) edge_weight = (int)tmp;
		if ((tmp = FindNumber(data, "edge_step=", (double) edge_step)) > -1) edge_step = (int)tmp;
		if ((tmp = FindNumber(data, "num_logo_buffers=", (double) num_logo_buffers)) > -1) num_logo_buffers = (int)tmp;
		if ((tmp = FindNumber(data, "use_existing_logo_file=", (double) useExistingLogoFile)) > -1) useExistingLogoFile = (int)tmp;
		if ((tmp = FindNumber(data, "two_pass_logo=", (double) startOverAfterLogoInfoAvail)) > -1) startOverAfterLogoInfoAvail = (bool) tmp;
		AddIniString("[Logo Interpretation]\n");
		if ((tmp = FindNumber(data, "connect_blocks_with_logo=", (double) connect_blocks_with_logo)) > -1) connect_blocks_with_logo = (int)tmp;
		if ((tmp = FindNumber(data, "logo_percentile=", (double) logo_percentile)) > -1) logo_percentile = (double)tmp;
		if ((tmp = FindNumber(data, "logo_fraction=", (double) logo_fraction)) > -1) logo_fraction = (double)tmp;
		if ((tmp = FindNumber(data, "shrink_logo=", (double) shrink_logo)) > -1) shrink_logo = (int)tmp;
		if ((tmp = FindNumber(data, "before_logo=", (double) before_logo)) > -1) before_logo = (int)tmp;
		if ((tmp = FindNumber(data, "after_logo=", (double) after_logo)) > -1) after_logo = (int)tmp;
		if ((tmp = FindNumber(data, "where_logo=", (double) where_logo)) > -1) where_logo = (int)tmp;
		if ((tmp = FindNumber(data, "min_black_frames_for_break=", (double) min_black_frames_for_break)) > -1) min_black_frames_for_break = (unsigned int)tmp;

		AddIniString("[Closed Captioning]\n");
		if ((tmp = FindNumber(data, "ccCheck=", (double) ccCheck)) > -1) ccCheck = (bool)tmp;
		if ((tmp = FindNumber(data, "cc_commercial_type_modifier=", (double) cc_commercial_type_modifier)) > -1) cc_commercial_type_modifier = (double)tmp;
		if ((tmp = FindNumber(data, "cc_wrong_type_modifier=", (double) cc_wrong_type_modifier)) > -1) cc_wrong_type_modifier = (double)tmp;
		if ((tmp = FindNumber(data, "cc_correct_type_modifier=", (double) cc_correct_type_modifier)) > -1) cc_correct_type_modifier = (double)tmp;
		AddIniString("[Live TV]\n");
		if ((tmp = FindNumber(data, "live_tv=", (double) live_tv)) > -1) live_tv = (bool) tmp;
		if ((tmp = FindNumber(data, "live_tv_retries=", (double) live_tv_retries)) > -1) live_tv_retries = (int) tmp;
		if ((tmp = FindNumber(data, "dvrms_live_tv_retries=", (double) dvrms_live_tv_retries)) > -1) dvrms_live_tv_retries = (int) tmp;
		if ((tmp = FindNumber(data, "standoff=", (double) standoff)) > -1) standoff = (int) tmp;
		if ((tmp = FindNumber(data, "dvrmsstandoff=", (double) dvrmsstandoff)) > -1) dvrmsstandoff = (int) tmp;
		if ((tmp = FindNumber(data, "require_div5=", (double) require_div5)) > -1) require_div5 = (bool) tmp;
		if ((tmp = FindNumber(data, "div5_tolerance=", (double) div5_tolerance)) > -1) div5_tolerance = tmp;
		if ((tmp = FindNumber(data, "incommercial_frames=", (double) incommercial_frames)) > -1) incommercial_frames = (int) tmp;

		

		AddIniString("[Output Control]\n");
		if ((tmp = FindNumber(data, "output_default=", (double) output_default)) > -1) output_default = (bool) tmp;
		if ((tmp = FindNumber(data, "output_chapters=", (double) output_chapters)) > -1) output_chapters = (bool) tmp;
		if ((tmp = FindNumber(data, "output_plist_cutlist=", (double) output_plist_cutlist)) > -1) output_plist_cutlist = (bool) tmp;
		if ((tmp = FindNumber(data, "output_zoomplayer_cutlist=", (double) output_zoomplayer_cutlist)) > -1) output_zoomplayer_cutlist = (bool) tmp;
		if ((tmp = FindNumber(data, "output_zoomplayer_chapter=", (double) output_zoomplayer_chapter)) > -1) output_zoomplayer_chapter = (bool) tmp;
		if ((tmp = FindNumber(data, "output_vcf=", (double) output_vcf)) > -1) output_vcf = (bool) tmp;
		if ((tmp = FindNumber(data, "output_vdr=", (double) output_vdr)) > -1) output_vdr = (bool) tmp;
		if ((tmp = FindNumber(data, "output_projectx=", (double) output_projectx)) > -1) output_projectx = (bool) tmp;
		if ((tmp = FindNumber(data, "output_avisynth=", (double) output_avisynth)) > -1) output_avisynth = (bool) tmp;
		if ((tmp = FindNumber(data, "output_videoredo=", (double) output_videoredo)) > -1) output_videoredo = (bool) tmp;
		if ((tmp = FindNumber(data, "videoredo_offset=", (double) videoredo_offset)) != -1) videoredo_offset = (int) tmp;
		if ((tmp = FindNumber(data, "output_btv=", (double) output_btv)) > -1) output_btv = (bool) tmp;
		if ((tmp = FindNumber(data, "output_edl=", (double) output_edl)) > -1) output_edl = (bool) tmp;
		if ((tmp = FindNumber(data, "edl_offset=", (double) edl_offset)) != -1) edl_offset = (int) tmp;
		if ((tmp = FindNumber(data, "output_edlp=", (double) output_edlp)) > -1) output_edlp = (bool) tmp;
		if ((tmp = FindNumber(data, "output_bsplayer=", (double) output_bsplayer)) > -1) output_bsplayer = (bool) tmp;
		if ((tmp = FindNumber(data, "output_edlx=", (double) output_edlx)) > -1) output_edlx = (bool) tmp;
		if ((tmp = FindNumber(data, "output_cuttermaran=", (double) output_cuttermaran)) > -1) output_cuttermaran = (bool) tmp;
		if ((tmp = FindNumber(data, "output_mpeg2schnitt=", (double) output_mpeg2schnitt)) > -1) output_mpeg2schnitt = (bool) tmp;
		if ((tmp = FindNumber(data, "output_womble=", (double) output_womble)) > -1) output_womble = (bool) tmp;
		if ((tmp = FindNumber(data, "output_mls=", (double) output_mls)) > -1) output_mls = (bool) tmp;
		if ((tmp = FindNumber(data, "output_mpgtx=", (double) output_mpgtx)) > -1) output_mpgtx = (bool) tmp;
		if ((tmp = FindNumber(data, "output_dvrmstb=", (double) output_dvrmstb)) > -1) output_dvrmstb = (bool) tmp;
		if ((tmp = FindNumber(data, "output_dvrcut=", (double) output_dvrcut)) > -1) output_dvrcut = (bool) tmp;
		if ((tmp = FindNumber(data, "output_ipodchap=", (double) output_ipodchap)) > -1) output_ipodchap = (bool) tmp;
		if ((tmp = FindNumber(data, "output_framearray=", (double) output_framearray)) > -1) output_framearray = (bool) tmp;
		if ((tmp = FindNumber(data, "output_debugwindow=", (double) output_debugwindow)) > -1) output_debugwindow = (bool) tmp;
		if ((tmp = FindNumber(data, "output_tuning=", (double) output_tuning)) > -1) output_tuning = (bool) tmp;
		if ((tmp = FindNumber(data, "output_training=", (double) output_training)) > -1) output_training = (bool) tmp;
		if ((tmp = FindNumber(data, "output_false=", (double) output_false)) > -1) output_false = (bool) tmp;
		if ((tmp = FindNumber(data, "output_aspect=", (double) output_aspect)) > -1) output_aspect = (bool) tmp;
		if ((tmp = FindNumber(data, "output_demux=", (double) output_demux)) > -1) output_demux = (bool) tmp;
		if ((tmp = FindNumber(data, "output_data=", (double) output_data)) > -1) output_data = (bool) tmp;
		if ((tmp = FindNumber(data, "output_srt=", (double) output_srt)) > -1) output_srt = (bool) tmp;
		if ((tmp = FindNumber(data, "output_smi=", (double) output_smi)) > -1) output_smi = (bool) tmp;
		if ((tmp = FindNumber(data, "output_timing=", (double) output_timing)) > -1) output_timing = (bool) tmp;
		if ((tmp = FindNumber(data, "delete_logo_file=", (double) deleteLogoFile)) > -1) deleteLogoFile = (int)tmp;

		if ((tmp = FindNumber(data, "cutscene_frame=", (double) cutsceneno)) > -1) cutsceneno = (int)tmp;
		if ((ts = FindString(data, "cutscene_dumpfile=", "")) != 0) strcpy(cutscenefile,ts);

		if ((tmp = FindNumber(data, "cutscene_threshold=", (double) cutscenedelta)) > -1) cutscenedelta = (int)tmp;
		if ((ts = FindString(data, "cutscenefile1=", "")) != 0) strcpy(cutscenefile1,ts);
		if (cutscenefile1[0]) LoadCutScene(cutscenefile1);
		if ((ts = FindString(data, "cutscenefile2=", "")) != 0) strcpy(cutscenefile2,ts);
		if (cutscenefile2[0]) LoadCutScene(cutscenefile2);
		if ((ts = FindString(data, "cutscenefile3=", "")) != 0) strcpy(cutscenefile3,ts);
		if (cutscenefile3[0]) LoadCutScene(cutscenefile3);
		if ((ts = FindString(data, "cutscenefile4=", "")) != 0) strcpy(cutscenefile4,ts);
		if (cutscenefile4[0]) LoadCutScene(cutscenefile4);
		if ((ts = FindString(data, "cutscenefile5=", "")) != 0) strcpy(cutscenefile5,ts);
		if (cutscenefile5[0]) LoadCutScene(cutscenefile5);
		if ((ts = FindString(data, "cutscenefile6=", "")) != 0) strcpy(cutscenefile6,ts);
		if (cutscenefile6[0]) LoadCutScene(cutscenefile6);
		if ((ts = FindString(data, "cutscenefile7=", "")) != 0) strcpy(cutscenefile7,ts);
		if (cutscenefile7[0]) LoadCutScene(cutscenefile7);
		if ((ts = FindString(data, "cutscenefile8=", "")) != 0) strcpy(cutscenefile8,ts);
		if (cutscenefile8[0]) LoadCutScene(cutscenefile8);

		
		if ((ts = FindString(data, "windowtitle=", windowtitle)) != 0) strcpy(windowtitle,ts);
		if ((ts = FindString(data, "cuttermaran_options=", cuttermaran_options)) != 0) strcpy(cuttermaran_options,ts);
		if ((ts = FindString(data, "mpeg2schnitt_options=", mpeg2schnitt_options)) != 0) strcpy(mpeg2schnitt_options,ts);
		if ((ts = FindString(data, "avisynth_options=", avisynth_options)) != 0) strcpy(avisynth_options,ts);
		if ((ts = FindString(data, "dvrcut_options=", dvrcut_options)) != 0) strcpy(dvrcut_options,ts);
		AddIniString("[Sage Workarounds]\n");
		if ((tmp = FindNumber(data, "sage_framenumber_bug=", (double) sage_framenumber_bug)) > -1) sage_framenumber_bug = (bool) tmp;
		if ((tmp = FindNumber(data, "sage_minute_bug=", (double) sage_minute_bug)) > -1) sage_minute_bug = (bool) tmp;
		if ((tmp = FindNumber(data, "enable_mencoder_pts=", (double) enable_mencoder_pts)) > -1) enable_mencoder_pts = (bool) tmp;
	} else {
		printf("No INI file found anywhere!!!!\n");
	}
	if (live_tv)
		output_incommercial = true;
	if (added_recording > 0 && giveUpOnLogoSearch < added_recording * 60) 
		giveUpOnLogoSearch += added_recording * 60;
}

FILE* LoadFile(char *f)
{
	char cwd[1000];
	char exe[1000];
	char *cmd[2];
	_getcwd(cwd, 256);
	sprintf(exe, "%s\\\%s", cwd, "Comskip.exe");
	cmd[0] = exe;
	cmd[1] = f;
	return( LoadSettings( 2, cmd));
}


FILE* LoadSettings(int argc, char ** argv) {
	char				data[20000];
	char				tempstr[MAX_PATH];
	char				cwd[MAX_PATH];
//	FILE*				ini_file = NULL;
	FILE*				logo_file = NULL;
	FILE*				log_file = NULL;
	FILE*				test_file = NULL;
	char *CEW_argv[10];
	size_t				len = 0;
	double				tmp;
	int					i = 0;
//	int					play_nice_start = -1;
//	int					play_nice_end = -1;
	time_t				ltime;
	struct tm*			now = NULL;
	int					mil_time;
	struct arg_lit*		cl_playnice				= arg_lit0("n", "playnice", "Slows detection down");
	struct arg_lit*		cl_output_zp_cutlist	= arg_lit0(NULL, "zpcut", "Outputs a ZoomPlayer cutlist");
	struct arg_lit*		cl_output_zp_chapter	= arg_lit0(NULL, "zpchapter", "Outputs a ZoomPlayer chapter file");
	struct arg_lit*		cl_output_vredo			= arg_lit0(NULL, "videoredo", "Outputs a VideoRedo cutlist");
	struct arg_lit*		cl_output_csv			= arg_lit0(NULL, "csvout", "Outputs a csv of the frame array");
	struct arg_lit*		cl_output_plist	= arg_lit0(NULL, "plist", "Outputs a mac-style plist for addition to an EyeTV archive as the 'markers' property");
	struct arg_int*		cl_detectmethod			= arg_intn("d", "detectmethod", NULL, 0, 1, "An integer sum of the detection methods to use");
//	struct arg_int*		cl_pid					= arg_intn("p", "pid", NULL, 0, 1, "The PID of the video in the TS");
	struct arg_str*		cl_pid					= arg_strn("p", "pid", NULL, 0, 1, "The PID of the video in the TS");
	struct arg_int*		cl_dump					= arg_intn("u", "dump", NULL, 0, 1, "Dump the cutscene at this frame number");
	struct arg_lit*		cl_ts					= arg_lit0("t", "ts", "The input file is a Transport Stream");
	struct arg_lit*		cl_help					= arg_lit0("h", "help", "Display syntax");
	struct arg_lit*		cl_show					= arg_lit0("s", "play", "Play the video");
	struct arg_lit*		cl_debugwindow			= arg_lit0("w", "debugwindow", "Show debug window");
	struct arg_lit*		cl_quiet				= arg_lit0("q", "quiet", "Not output logging to the console window");
	struct arg_lit*		cl_demux				= arg_lit0("m", "demux", "Demux the input into elementary streams");
	struct arg_int*		cl_verbose				= arg_intn("v", "verbose", NULL, 0, 1, "Verbose level");
	struct arg_file*	cl_ini					= arg_filen(NULL, "ini", NULL, 0, 1, "Ini file to use");
	struct arg_file*	cl_logo					= arg_filen(NULL, "logo", NULL, 0, 1, "Logo file to use");
	struct arg_file*	cl_cut					= arg_filen(NULL, "cut", NULL, 0, 1, "CutScene file to use");
	struct arg_file*	in						= arg_filen(NULL, NULL, NULL, 1, 1, "Input file");
	struct arg_file*	out						= arg_filen(NULL, NULL, NULL, 0, 1, "Output directory");
	struct arg_end*		end						= arg_end(20);
	void*				argtable[] =
	{
		cl_help,
		cl_debugwindow,
		cl_playnice,
		cl_output_zp_cutlist,
		cl_output_zp_chapter,
		cl_output_vredo,
		cl_output_csv,
    cl_output_plist,
		cl_demux,
		cl_pid,
		cl_ts,
		cl_detectmethod,
		cl_verbose,
		cl_dump,
		cl_show,
		cl_quiet,
		cl_ini,
		cl_logo,
		cl_cut,
		in,
		out,
		end
	};
	int					nerrors;
	int					exitcode = 0;
  incomingCommandLine[0]=0; // was:	sprintf(incomingCommandLine, "");
	if (strchr(argv[0], ' ')) {
		sprintf(incomingCommandLine, "\"%s\"", argv[0]);
	} else {
		sprintf(incomingCommandLine, "%s", argv[0]);
	}

	for (i = 1; i < argc; i++) {
		sprintf(tempstr, "%s", incomingCommandLine);
		if (strchr(argv[i], ' ')) {
			sprintf(incomingCommandLine, "%s \"%s\"", tempstr, argv[i]);
		} else {
			sprintf(incomingCommandLine, "%s %s", tempstr, argv[i]);
		}
	}

	printf("The commandline used was:\n%s\n\n", incomingCommandLine);
	argument = malloc(sizeof(char *) * argc);
	argument_count = argc;
	for (i = 0; i < argc; i++) {
		argument[i] = malloc(sizeof(char) * (strlen(argv[i]) + 1));
		strcpy(argument[i], argv[i]);
	}

	if (argc <= 1) {

#ifdef COMSKIPGUI
//			output_debugwindow = true;
#endif
			
			if (strstr(argv[0],"GUI"))
				output_debugwindow = true;			
			if (output_debugwindow) {
// This is a trick to ask for a input filename when no argument has been given.
//				while (mpegfilename[0] == 0)
//					ReviewResult();
//				argc++;
//				strcpy(argument[1], mpegfilename);
			}
	}



	// verify the argtable[] entries were allocated sucessfully
	if (arg_nullcheck(argtable) != 0) {

		// NULL entries were detected, some allocations must have failed
		Debug(0, "%s: insufficient memory\n", progname);
		exitcode = 1;
		goto exit;
	}

	nerrors = arg_parse(argc, argv, argtable);
	if (cl_help->count) {
		printf("Usage:\n  comskip ");
		arg_print_syntaxv(stdout, argtable, "\n\n");
		arg_print_glossary(stdout, argtable, "  %-25s %s\n");
		printf("\nDetection Methods\n");
		printf("\t%3i - Black Frame\n", BLACK_FRAME);
		printf("\t%3i - Logo\n", LOGO);
		printf("\t%3i - Scene Change\n", SCENE_CHANGE);
		printf("\t%3i - Resolution Change\n", RESOLUTION_CHANGE);
		printf("\t%3i - Closed Captions\n", CC);
		printf("\t%3i - Aspect Ratio\n", AR);
		printf("\t%3i - Silence\n", SILENCE);
		printf("\t%3i - CutScenes\n", CUTSCENE);
		printf("\t255 - USE ALL AVAILABLE\n");
		exit(2);
	}

	if (nerrors) {
		printf("Usage:\n  comskip ");
		arg_print_syntaxv(stdout, argtable, "\n\n");
		arg_print_glossary(stdout, argtable, "  %-25s %s\n");
		printf("\nDetection methods available:\n");
		printf("\t%3i - Black Frame\n", BLACK_FRAME);
		printf("\t%3i - Logo\n", LOGO);
		printf("\t%3i - Scene Change\n", SCENE_CHANGE);
		printf("\t%3i - Resolution Change\n", RESOLUTION_CHANGE);
		printf("\t%3i - Closed Captions\n", CC);
		printf("\t%3i - Aspect Ratio\n", AR);
		printf("\t%3i - Silence\n", SILENCE);
		printf("\t%3i - CutScenes\n", CUTSCENE);
		printf("\t255 - USE ALL AVAILABLE\n");
		printf("\nErrors:\n");
		arg_print_errors(stdout, end, "ComSkip");
		exit(2);
	}

	if (strcmp(in->extension[0], ".mpg") == 0 || strcmp(in->extension[0], ".mpeg") == 0 || strcmp(in->extension[0], ".ts") == 0 || strcmp(in->extension[0], ".tp") == 0 || strcmp(in->extension[0], ".dvr-ms") == 0) {
		if (strcmp(in->extension[0], ".ts") == 0)
			demux_pid = 1;
		if (strcmp(in->extension[0], ".tp") == 0)
			demux_pid = 1;
		if (strcmp(in->extension[0], ".dvr-ms") == 0)
			demux_asf = 1;

		sprintf(mpegfilename, "%s", in->filename[0]);
		in_file = fopen(in->filename[0], "rb");
		printf("Opening %s\n", in->filename[0]);
		if (!in_file) {
			fprintf(stderr, "%s - could not open file %s\n", strerror(errno), in->filename[0]);
			exit(3);
		}
		_stat(in->filename[0], &instat);
    sprintf(basename, "%.*s", (int)strlen(in->filename[0]) - (int)strlen(in->extension[0]), in->filename[0]);
		i = strlen(basename);
		while (i>0 && basename[i-1] != '\\' && basename[i-1] != '/') {
			i--;
		}
		strcpy(shortbasename, &basename[i]);
		sprintf(inifilename, "%.*scomskip.ini", i, basename);
	} else if (strcmp(in->extension[0], ".csv") == 0) {
		loadingCSV = true;
		in_file = fopen(in->filename[0], "r");
		printf("Opening %s array file.\n", in->filename[0]);
		if (!in_file) {
			fprintf(stderr, "%s - could not open file %s\n", strerror(errno), in->filename[0]);
			exit(4);
		}

    sprintf(basename,     "%.*s", (int)strlen(in->filename[0]) - (int)strlen(in->extension[0]), in->filename[0]);
    sprintf(mpegfilename, "%.*s.mpg", (int)strlen(basename), basename);
		test_file = fopen(mpegfilename, "rb");
		if (!test_file) {
			sprintf(mpegfilename, "%.*s.ts", (int)strlen(basename), basename);
			test_file = fopen(mpegfilename, "rb");
			if (!test_file) {
				sprintf(mpegfilename, "%.*s.tp", (int)strlen(basename), basename);
				test_file = fopen(mpegfilename, "rb");
				if (!test_file) {
					sprintf(mpegfilename, "%.*s.dvr-ms", (int)strlen(basename), basename);
					test_file = fopen(mpegfilename, "rb");
					if (!test_file) {
						mpegfilename[0] = 0;
					} else {
						demux_asf = 1;
						fclose(test_file);
					}
				}
				else
					fclose(test_file);
			}
			else
				fclose(test_file);
		}
		else
			fclose(test_file);

		i = strlen(basename);
		while (i>0 && basename[i-1] != '\\' && basename[i-1] != '/') {
			i--;
		}
		strcpy(shortbasename, &basename[i]);
		sprintf(inifilename, "%.*scomskip.ini", i, basename);
		if (mpegfilename[0] == 0) sprintf(mpegfilename, "%s.mpg", basename);
	} else if (strcmp(in->extension[0], ".txt") == 0) {
		loadingTXT = true;
		output_default = false;
		in_file = fopen(in->filename[0], "r");
		printf("Opening %s for review\n", in->filename[0]);
		if (!in_file) {
			fprintf(stderr, "%s - could not open file %s\n", strerror(errno), in->filename[0]);
			exit(4);
		}
		fclose(in_file);
		in_file = 0;

    sprintf(basename,     "%.*s", (int)strlen(in->filename[0]) - (int)strlen(in->extension[0]), in->filename[0]);
    sprintf(mpegfilename, "%.*s.mpg", (int)strlen(basename), basename);
		test_file = fopen(mpegfilename, "rb");
		if (!test_file) {
			sprintf(mpegfilename, "%.*s.ts", (int)strlen(basename), basename);
			test_file = fopen(mpegfilename, "rb");
			if (!test_file) {
				sprintf(mpegfilename, "%.*s.tp", (int)strlen(basename), basename);
				test_file = fopen(mpegfilename, "rb");
				if (!test_file) {
					sprintf(mpegfilename, "%.*s.dvr-ms", (int)strlen(basename), basename);
					demux_asf = 1;
				} else
					fclose(test_file);
			} else
				fclose(test_file);
		} else
			fclose(test_file);
		
		i = strlen(basename);
		while (i>0 && basename[i-1] != '\\') {
			i--;
		}
		strcpy(shortbasename, &basename[i]);
		sprintf(inifilename, "%.*scomskip.ini", i, basename);
//		sprintf(mpegfilename, "%s.mpg", basename);
	} else {
		printf("The input file was not an MPEG file or comskip CSV or TXT file - %s.\n", in->extension[0]);
		exit(5);
	}
	if (cl_ini->count) {
		sprintf(inifilename, cl_ini->filename[0]);
		printf("Setting ini file to %s as per commandline\n", inifilename);
	}

	ini_file = fopen(inifilename, "r");

	sprintf(logofilename, "%s.logo.txt", basename);
	sprintf(logfilename, "%s.log", basename);
	sprintf(filename, "%s.txt", basename);
	if (strcmp(HomeDir, ".") == 0) {
		if (!ini_file) {
			sprintf(inifilename, "comskip.ini");
			ini_file = fopen(inifilename, "r");
		}
		sprintf(exefilename, "comskip.exe");
		sprintf(dictfilename, "comskip.dictionary");
	} else {
		if (!ini_file) {
			sprintf(inifilename, "%s\\comskip.ini", HomeDir);
			ini_file = fopen(inifilename, "r");
		}
		sprintf(exefilename, "%s\\comskip.exe", HomeDir);
		sprintf(dictfilename, "%s\\comskip.dictionary", HomeDir);
	}

	if (cl_cut->count) {
		printf("Loading cutfile %s as per commandline\n", cl_cut->filename[0]);
		LoadCutScene(cl_cut->filename[0]);
	}

	if (cl_logo->count) {
		sprintf(logofilename, cl_logo->filename[0]);
		printf("Setting logo file to %s as per commandline\n", logofilename);
	}

	if (out->count) {
		sprintf(outputfilename, out->filename[0]);
		i = strlen(outputfilename);
		if (outputfilename[i-1] == '\\')
			outputfilename[i-1] = 0;
	}
	else
		outputfilename[0] = 0;

	//	if (!loadingTXT)
		LoadIniFile();

//	live_tv = true;

	time(&ltime);
	now = localtime(&ltime);
	mil_time = (now->tm_hour * 100) + now->tm_min;
	if ((play_nice_start > -1) && (play_nice_end > -1)) {
		if (play_nice_start > play_nice_end) {
			if ((mil_time >= play_nice_start) || (mil_time <= play_nice_end)) play_nice = true;
		} else {
			if ((mil_time >= play_nice_start) && (mil_time <= play_nice_end)) play_nice = true;
		}
	}

	if (cl_verbose->count) {
		verbose = cl_verbose->ival[0];
		printf("Setting verbose level to %i as per command line.\n", verbose);
	}

	if (cl_debugwindow->count || loadingTXT) {
		output_debugwindow = true;
	}

	if (cl_show->count) {
		subsample_video = 0;
		output_debugwindow = true;
	}
	
	if (cl_quiet->count) {
		output_console = false;
	}
	

#ifdef COMSKIPGUI
//		output_debugwindow = true;
#endif

	if (strstr(argv[0],"GUI"))
		output_debugwindow = true;

	if (cl_demux->count) {
		output_demux = true;
	}

	if (!loadingTXT && !useExistingLogoFile && cl_logo->count==0) {
		logo_file = fopen(logofilename, "r");
		if(logo_file) {
			fclose(logo_file);
			remove(logofilename);
		}
	}

	if (cl_output_csv->count) {
		output_framearray = true;
	}



	if (verbose) {
		logo_file = fopen(logofilename, "r");
		if (loadingTXT) {
			// Do nothing to the log file
			verbose = 0;
		} else if (loadingCSV) {
			log_file = fopen(logfilename, "w");
			fprintf(log_file, "################################################################\n");
			fprintf(log_file, "Generated using Comskip %s.%s\n", COMSKIPVERSION,SUBVERSION);
			fprintf(log_file, "Loading comskip csv file - %s\n", in->filename[0]);
			fprintf(log_file, "Time at start of run:\n%s", ctime(&ltime));
			fprintf(log_file, "################################################################\n");
			fclose(log_file);
			log_file = NULL;
		} else if (logo_file) {
			fclose(logo_file);
			log_file = fopen(logfilename, "a+");
			fprintf(log_file, "################################################################\n");
			fprintf(log_file, "Starting second pass using %s\n", logofilename);
			fprintf(log_file, "Time at start of second run:\n%s", ctime(&ltime));
			fprintf(log_file, "################################################################\n");
			fclose(log_file);
			log_file = NULL;
		} else {
			log_file = fopen(logfilename, "w");
			fprintf(log_file, "################################################################\n");
			fprintf(log_file, "Generated using Comskip %s.%s\n", COMSKIPVERSION,SUBVERSION);
			fprintf(log_file, "Time at start of run:\n%s", ctime(&ltime));
			fprintf(log_file, "################################################################\n");
			fclose(log_file);
			log_file = NULL;
		}
	}

	if (cl_playnice->count) {
		play_nice = true;
		Debug(1, "ComSkip playing nice due as per command line.\n");
	}

	if (cl_detectmethod->count) {
		commDetectMethod = cl_detectmethod->ival[0];
		printf("Setting detection methods to %i as per command line.\n", commDetectMethod);
	}

	if (cl_dump->count) {
		cutsceneno = cl_dump->ival[0];
		printf("Setting dump frame number to %i as per command line.\n", cutsceneno);
	}

	if (cl_ts->count) {
		demux_pid = 1;
		printf("Auto selecting the PID.\n");
	}

	if (cl_pid->count) {
//		demux_pid = cl_pid->ival[0];
		sscanf(cl_pid->sval[0],"%x", &demux_pid);
		printf("Selecting PID %x as per command line.\n", demux_pid);
	}



	Debug(9, "Mpeg:\t%s\nExe\t%s\nLogo:\t%s\nIni:\t%s\n", mpegfilename, exefilename, logofilename, inifilename);
	Debug(1, "\nDetection Methods to be used:\n");
	i = 0;
	if (commDetectMethod & BLACK_FRAME) {
		i++;
		Debug(1, "\t%i) Black Frame\n", i);
	}

	if (commDetectMethod & LOGO) {
		i++;
		Debug(1, "\t%i) Logo - Give up after %i seconds\n", i, giveUpOnLogoSearch);
	}

	if (commDetectMethod & CUTSCENE) {
//		commDetectMethod &= ~SCENE_CHANGE;
	}

	if (commDetectMethod & SCENE_CHANGE) {
		i++;
		Debug(1, "\t%i) Scene Change\n", i);
	}

	if (commDetectMethod & RESOLUTION_CHANGE) {
		i++;
		Debug(1, "\t%i) Resolution Change\n", i);
	}

	if (commDetectMethod & CC) {
		i++;
		processCC = true;
		Debug(1, "\t%i) Closed Captions\n", i);
	}

	if (commDetectMethod & AR) {
		i++;
		Debug(1, "\t%i) Aspect Ratio\n", i);
	}

	if (commDetectMethod & SILENCE) {
		i++;
		Debug(1, "\t%i) Silence\n", i);
	}

	if (commDetectMethod & CUTSCENE) {
		i++;
		Debug(1, "\t%i) CutScenes\n", i);
	}


	Debug(1, "\n");
	if (play_nice_start || play_nice_end) {
		Debug(
			1,
			"\nComSkip throttles back from %.4i to %.4i.\nThe time is now %.4i ",
			play_nice_start,
			play_nice_end,
			mil_time
		);
		if (play_nice) {
			Debug(1, "so comskip is running slowly.\n");
		} else {
			Debug(1, "so it's full speed ahead!\n");
		}
	}

	Debug(10, "\nSettings\n--------\n");
	Debug(10, "%s\n", ini_text);
	sprintf(out_filename, "%s.txt", basename);


	if (!loadingTXT) {
		logo_file = fopen(logofilename, "r+");
		if (logo_file) {
			Debug(1, "The logo mask file exists.\n");
			fclose(logo_file);
			LoadLogoMaskData();
		}
	}

	out_file = plist_cutlist_file = zoomplayer_cutlist_file = zoomplayer_chapter_file = vcf_file = vdr_file = projectx_file = avisynth_file = cuttermaran_file = videoredo_file = btv_file = edl_file = ipodchap_file = edlp_file = edlx_file = mls_file = womble_file = mpgtx_file = dvrcut_file = dvrmstb_file = tuning_file = training_file = 0L;

	if (cl_output_plist->count)
		output_plist_cutlist = true;
	if (cl_output_zp_cutlist->count)
		output_zoomplayer_cutlist = true;
	if (cl_output_zp_chapter->count)
		output_zoomplayer_chapter = true;
	if (cl_output_vredo->count)
		output_videoredo = true;
	if (cl_output_plist->count)
		output_plist_cutlist = true;

	if (output_default && ! loadingTXT) {
		if(!isSecondPass) {
			out_file = fopen(out_filename, "w");
			if (!out_file) {
				fprintf(stderr, "%s - could not create file %s\n", strerror(errno), filename);
				exit(6);
			} else {
				output_default = true;
				fclose(out_file);
			}
		}
	}

//	max_commercialbreak *= fps;
//	min_commercialbreak *= fps;
//	max_commercial_size *= fps;
//	min_commercial_size *= fps;

	if (loadingTXT) {
		frame_count = InputReffer(".txt");
		if (frame_count < 0) {
			printf("Incompatible TXT file\n");
			exit(2);			
		}
		framearray = NULL;
		printf("Close window or hit ESCAPE when done\n");
		output_debugwindow = true;
		ReviewResult();
//		in_file = NULL;
	}

	if (!loadingTXT && (output_srt || output_smi )) {
		i = 0;
		CEW_argv[i++] = "comskip.exe";
		if (output_smi) {
			CEW_argv[i++] = "-sami";
			output_srt = 1;
		}
		else
			CEW_argv[i++] = "-srt";
		CEW_argv[i++] = in->filename[0];
		CEW_init (i, CEW_argv);
	}

	
	if (loadingCSV) {
		output_framearray = false;
		ProcessCSV(in_file);
		output_debugwindow = false;
	}


exit:
	// deallocate each non-null entry in argtable[]
	arg_freetable(argtable, sizeof(argtable) / sizeof(argtable[0]));
	return (in_file);
}

/*
#ifdef notused

int GetAvgBrightness(void) {
	int brightness = 0;
	int pixels = 0;
	int x;
	int y;
	for (y = border; y < (height - border); y += 4) {
		for (x = border; x < (width - border); x += 4) {
			brightness += frame_ptr[y * width + x];
			pixels++;
		}
	}

	return (brightness / pixels);
}

bool CheckFrameIsBlack(void) {
	int			x;
	int			y;
	int			pass;
	int			avg = 0;
	const int	pass_start[7] = { 0, 4, 0, 2, 0, 1, 0 };
	const int	pass_inc[7] = { 8, 8, 4, 4, 2, 2, 1 };
	const int	pass_ystart[7] = { 0, 0, 4, 0, 2, 0, 1 };
	const int	pass_yinc[7] = { 8, 8, 8, 4, 4, 2, 2 };
	bool		isDim = false;
	int			dimCount = 0;
	int			pixelsChecked = 0;
	int			curMaxBright = 0;
	if (!width || !height) return (false);
	avg = GetAvgBrightness();

	// go through the image in png interlacing style testing if black
	// skip region 'border' pixels wide/high around border of image.
	for (pass = 0; pass < 7; pass++) {
		for (y = pass_ystart[pass] + border; y < (height - border); y += pass_yinc[pass]) {
			for (x = pass_start[pass] + border; x < (width - border); x += pass_inc[pass]) {
				pixelsChecked++;
				if (frame_ptr[y * width + x] > max_brightness) return (false);
				if (frame_ptr[y * width + x] > test_brightness) {
					isDim = true;
					dimCount++;
				}
			}
		}
	}

	if ((dimCount > (int)(.05 * pixelsChecked)) && (dimCount < (int)(.35 * pixelsChecked))) return (false);

	// frame is dim so test average
	if (isDim) {
		if (avg > max_avg_brightness) return (false);
	}

	brightHistogram[avg]++;
	InitializeBlackArray(black_count);
	black[black_count].frame = framenum_real;
	black[black_count].brightness = avg;
	black[black_count].uniform = 0;
	black[black_count].volume = curvolume;
	if (avg < min_brightness_found) min_brightness_found = avg;
	black_count++;
	Debug(5, "Frame %6i - Black frame with brightness of %i\n", framenum_real, avg);
	return (true);
}

void BuildBlackFrameCommList(void) {
	long		c_start[MAX_COMMERCIALS];
	long		c_end[MAX_COMMERCIALS];
	long		ic_start[MAX_COMMERCIALS];
	long		ic_end[MAX_COMMERCIALS];
	int			commercials = 0;
	int			i;
	int			j;
	int			k;
	int			x;
	int			len;
	int			marked = 0;
	double		remainder;
	double		added;
	bool		oldbreak;
	static int	runs = 0;
	if (black_count == 0) return;

	// detect individual commercials from black frames
	for (i = 0; i < black_count; i++) {
		for (x = i + 1; x < black_count; x++) {
			int gap_length = black[x].frame - black[i].frame;
			if (gap_length < min_commercial_size * fps) continue;
			oldbreak = commercials > 0 && ((black[i].frame - c_end[commercials - 1]) < 10 * fps);
			if (gap_length > max_commercialbreak * fps ||
				(!oldbreak && gap_length > max_commercial_size * fps) ||
				(oldbreak && (black[x].frame - c_end[commercials - 1] > max_commercial_size * fps)))
				break;

			// if((!require_div5) || ((int)((float)gap_length/fps + .5) %5 ==
			// 0)) // look for segments in multiples of 5 seconds
			added = gap_length / fps + div5_tolerance;
			remainder = added - 5 * ((int)(added / 5.0));
			Debug(4, "%i,%i,%i: %.2f,%.2f\n", black[i].frame, black[x].frame, gap_length, gap_length / fps, remainder);
			if ((require_div5 != 1) || (remainder >= 0 && remainder <= 2 * div5_tolerance)) {

				// look for segments in multiples of 5 seconds
				if (oldbreak) {
					if (black[x].frame > c_end[commercials - 1] + fps) {

						// added = (black[x].frame -
						// c_end[commercials-1])/fps;
						c_end[commercials - 1] = black[x].frame;
						ic_end[commercials - 1] = x;
						Debug(
							1,
							"--start: %i, end: %i, len: %.2fs\t%.2fs\n",
							black[i].frame,
							black[x].frame,
							(black[x].frame - black[i].frame) / fps,
							(c_end[commercials - 1] - c_start[commercials - 1]) / fps
						);
					}
				} else {

					// new break
					Debug(
						1,
						"\n  start: %i, end: %i, len: %.2fs\n",
						black[i].frame,
						black[x].frame,
						((black[x].frame - black[i].frame) / fps)
					);
					ic_start[commercials] = i;
					ic_end[commercials] = x;
					c_start[commercials] = black[i].frame;
					c_end[commercials++] = black[x].frame;
					Debug(
						1,
						"\n  start: %i, end: %i, len: %is\n",
						c_start[commercials - 1],
						c_end[commercials - 1],
						(int)((c_end[commercials - 1] - c_start[commercials - 1]) / fps)
					);
				}

				i = x - 1;
				x = black_count;
			}
		}
	}

	Debug(1, "\n");
	if (verbose == 3 && runs == 0) {

		// list all black scene breaks
		marked = 0;
		commercials = 0;
		for (i = 0; i < black_count; i++) {
			if ((black[i].frame - marked) > 5 * fps) {
				marked = black[i].frame;
				commercials++;
				Debug(1, "%i: %i\n", commercials, marked);
			}
		}

		Debug(1, "\n\n");
	}

	if (verbose == 4 && runs == 0) {
		for (i = 0; i < black_count; i++) {
			Debug(1, "%i\n", black[i].frame);
		}

		Debug(1, "\n\n");
	}

	if (runs > 0 || require_div5 < 2) {
		Debug(1, "--------------------\n");
	}

	// print out commercial breaks skipping those that are too small or too large
	for (i = 0; i < commercials; i++) {
		len = c_end[i] - c_start[i];
		if ((len >= (int)min_commercialbreak * fps) && (len <= (int)max_commercialbreak * fps)) {

			// find the middle of the scene change, max 3 seconds.
			j = ic_start[i];
			while ((j > 0) && ((black[j].frame - black[j - 1].frame) == 1)) {

				// find beginning
				j--;
			}

			for (k = j; k < black_count; k++) {

				// find end
				if ((black[k].frame - black[j].frame) > (int)(3 * fps)) {
					break;
				}
			}

			x = j + (int)((k - j) / 2);
			c_start[i] = black[x].frame;
			j = ic_end[i];
			while ((j < black_count) && ((black[j + 1].frame - black[j].frame) == 1)) {

				// find end
				j++;
			}

			for (k = j; k > 0; k--) {

				// find start
				if (black[j].frame - (black[k].frame) > (int)(3 * fps)) {
					break;
				}
			}

			x = k + (int)((j - k) / 2);
			c_end[i] = black[x].frame - 1;
			Debug(4, "%i - start: %i   end: %i\n", i + 1, c_start[i], c_end[i]);
			if (require_div5 != 2) OutputCommercialBlock(c_start[i], c_end[i]);
		}
	}

	if (require_div5 == 2) {
		require_div5 = 1;
		runs++;
		BuildBlackFrameCommList();
	}
}

#endif
*/

void ProcessARInfoInit(int minY, int maxY, int minX, int maxX)
{
	double pictureHeight = maxY - minY;
	double pictureWidth = maxX - minX;

	if (minX <= border) minX = 1;
	if (minY <= border) minY = 1;
	if (maxY >= height - border) maxY = height;
	if (maxX >= width - border) maxX = width;
	
	/*
	ar_width = width;
	if (ar_width < maxY + minY)
		ar_width = (int)((maxY + minY) * 1.3);
*/	
	
	last_ar_ratio = (double)(pictureWidth) / (double)pictureHeight;
	last_ar_ratio = ceil(last_ar_ratio * ar_rounding) / ar_rounding;
	ar_ratio_trend = last_ar_ratio;
	if (last_ar_ratio < 0.5 || last_ar_ratio > 3.0)
		last_ar_ratio = AR_UNDEF;
//	lastAR = (last_ar_ratio <= ar_split);
	ar_ratio_start = framenum_real;			
	ar_block[ar_block_count].start = framenum_real;
	ar_block[ar_block_count].width = width;
	ar_block[ar_block_count].height = height;
	ar_block[ar_block_count].minX = minX;
	ar_block[ar_block_count].minY = minY;
	ar_block[ar_block_count].maxX = maxX;
	ar_block[ar_block_count].maxY = maxY;
//			ar_block[ar_block_count].ar = lastAR;
	ar_block[ar_block_count].ar_ratio = last_ar_ratio;
//	if (framearray) frame[frame_count].ar_ratio = last_ar_ratio;;
	Debug(4, "Frame: %i\tRatio: %.2f\tMinY: %i MaxY: %i MinX: %i MaxX: %i\n", ar_ratio_start, ar_ratio_trend , minY, maxY, minX, maxX);	

//	Debug(4, "\nFirst Frame\nFrame: %i\tMinY: %i\tMaxY: %i\tRatio: %.2f\n", framenum_real, minY, maxY, last_ar_ratio);
}

void ProcessARInfo(int minY, int maxY, int minX, int maxX)
{
	int		pictureHeight, pictureWidth;
	int		hi,i;
	double	cur_ar_ratio;

	if (minX <= border) minX = 1;
	if (minY <= border) minY = 1;
	if (maxY >= height - border) maxY = height;
	if (maxX >= width - border) maxX = width;
	
	
	if (ticker_tape > 0 || ( 
			abs((height - maxY) - (minY)) < 13 + (minY )/15  &&  // discard for no simetrical check
			abs((width  - maxX) - (minX)) < 13 + (minX )/15  &&  // discard for no simetrical check
			minY < height / 4 &&
			minX < width / 4)
		) // check if simetrical
	{

		pictureHeight = maxY - minY;
		pictureWidth = maxX - minX;
		cur_ar_ratio = (double)(pictureWidth) / (double)pictureHeight;
		cur_ar_ratio = ceil(cur_ar_ratio * ar_rounding) / ar_rounding;
		if (cur_ar_ratio > 3.0 || cur_ar_ratio < 0.5) 
			cur_ar_ratio = AR_UNDEF;

		hi = (int)((cur_ar_ratio - 0.5)*100);
		if (hi >= 0 && hi < MAX_ASPECT_RATIOS) {
			ar_histogram[hi].frames += 1;
			ar_histogram[hi].ar_ratio = cur_ar_ratio;
		}


		if (cur_ar_ratio - last_ar_ratio > ar_delta || cur_ar_ratio - last_ar_ratio < -ar_delta)
		{
			if (cur_ar_ratio - ar_ratio_trend < ar_delta && cur_ar_ratio - ar_ratio_trend > -ar_delta) 
			{								// Same ratio as previous trend
				ar_ratio_trend_counter++;
				if (ar_ratio_trend_counter > AR_TREND)
				{
					last_ar_ratio = ar_ratio_trend;
					ar_ratio_trend_counter = 0;
					ar_misratio_trend_counter = 0;
					if (commDetectMethod & AR) {

						ar_block[ar_block_count].end = ar_ratio_start-1;
						ar_block_count++;
						InitializeARBlockArray(ar_block_count);
						ar_block[ar_block_count].start = ar_ratio_start;
						ar_block[ar_block_count].ar_ratio = ar_ratio_trend;
						ar_block[ar_block_count].volume = 0;
						ar_block[ar_block_count].width = width;
						ar_block[ar_block_count].height = height;
						ar_block[ar_block_count].minX = minX;
						ar_block[ar_block_count].minY = minY;
						ar_block[ar_block_count].maxX = maxX;
						ar_block[ar_block_count].maxY = maxY;
					}
					Debug(9, "Frame: %i\tRatio: %.2f\tMinY: %i\tMaxY: %i\tMinX: %i\tMaxX: %i\n", ar_ratio_start, ar_ratio_trend , minY, maxY, minX, maxX);	
					last_ar_ratio = ar_ratio_trend;
					if (framearray) {
						for (i = ar_ratio_start; i < framenum_real; i++) {
							frame[i].ar_ratio = last_ar_ratio;
						}
					}
				}
			} else {						// Other ratio as previous trend
				ar_ratio_trend = cur_ar_ratio;
				ar_ratio_trend_counter = 0;
				ar_ratio_start = framenum_real;			
			}
			ar_misratio_trend_counter++;
		} else {							// Same ratio as previous frame
			ar_ratio_trend_counter = 0;
			ar_ratio_start = framenum_real;			
			ar_misratio_trend_counter = 0;
			ar_misratio_start = framenum_real;
		}

	}
	else
	{										// Unreliable ratio
//		ar_ratio_trend = cur_ar_ratio;
		ar_ratio_trend_counter = 0;
		ar_ratio_start = framenum_real;			
/*
  //	if (framearray) frame[frame_count].minY = 0;
	//	if (framearray) frame[frame_count].maxY = height;
	//	Debug(9, "Frame: %i\tAsimetrical\tMinY: %i\tMaxY: %i\n", framenum_real, minY, maxY);
		ar_ratio_trend_counter = 0;
		ar_ratio_start = framenum_real;			
		ar_misratio_trend_counter++;
		if (framearray) frame[frame_count].ar_ratio = 0.0;
*/
	
	}
/*
	if (last_ar_ratio == 0) {
		ar_misratio_trend_counter = 0;

	} else {
*/
		if (ar_misratio_trend_counter > 3*fps && last_ar_ratio != AR_UNDEF) {
			last_ar_ratio = ar_ratio_trend = AR_UNDEF;
			if (commDetectMethod & AR) {
				ar_block[ar_block_count].end = framenum_real - 3*(int)fps -1;
				ar_block_count++;
				InitializeARBlockArray(ar_block_count);
				ar_block[ar_block_count].start = framenum_real - 3*(int)fps;
				ar_block[ar_block_count].ar_ratio = ar_ratio_trend;
				ar_block[ar_block_count].volume = 0;
				ar_block[ar_block_count].width = width;
				ar_block[ar_block_count].height = height;
				ar_block[ar_block_count].minX = minX;
				ar_block[ar_block_count].minY = minY;
				ar_block[ar_block_count].maxX = maxX;
				ar_block[ar_block_count].maxY = maxY;
			}
			Debug(9, "Frame: %i\tRatio: %.2f\tMinY: %i\tMaxY: %i\n", ar_ratio_start, ar_ratio_trend , minY, maxY);	
			last_ar_ratio = ar_ratio_trend;
		}
//	}	
	
	
//	if (framearray) frame[frame_count].ar_ratio = last_ar_ratio;
}


int MatchCutScene(unsigned char *cutscene)
{
	int i,j,x,y,d;
	int delta = 0;
	int step = 4;
	int c=0;
	if (width > 800) step = 8;
	if (width < 400) step = 2;
	for (y = border; y < (height - border); y += step) {
		for (x = border; x < (width - border); x += step) {
			if (c < MAXCSLENGTH) {
				d = (int)frame_ptr[y * width + x] - (int)(cutscene[c]);
				if (d > edge_level_threshold || d < -edge_level_threshold)
					delta += 1;
			}
			c++;
		}
	}
	return(delta);
}

void RecordCutScene(int frame_count, int brightness)
{
	char cs[MAXCSLENGTH];
	int c;
	int i,j,x,y;
	int delta = 0;
	int step = 4;

	if (width > 800) step = 8;
	if (width < 400) step = 2;
	c = 0;
	for (y = border; y < (height - border); y += step) {
		for (x = border; x < (width - border); x += step) {
			if (c < MAXCSLENGTH) {
				cs[c++] = frame_ptr[y * width + x];
			}
		}
	}
	cutscene_file = NULL;
	if (osname[0]) {
		strcpy(cutscenefile, osname);
		strcat(cutscenefile, ".dmp");
	}
	if (cutscenefile[0]) {
		cutscene_file = fopen(cutscenefile,"wb");
	}
	if (cutscene_file != NULL) {
		fwrite(&brightness, sizeof(int), 1, cutscene_file);
		fwrite(cs, sizeof(char), c, cutscene_file);
		Debug(7, "Saved frame %6i into cutfile \"%s\"\n", frame_count, cutscenefile);
		fclose(cutscene_file);
		cutscene_file = NULL;
	}
}

void LoadCutScene(const char *filename)
{
	int i,j,s,b,c;
	cutscene_file = fopen(filename,"rb");
	if (cutscene_file != NULL) {
		i = cutscenes;
		    fread(&csbrightness[i], sizeof(int), 1, cutscene_file);
		c =	fread(cutscene[i], sizeof(char), MAXCSLENGTH, cutscene_file);
		if (c > 0) {
			Debug(7, "Loaded %i bytes from cutfile \"%s\"\n", c, filename);
			cslength[i] = c;
			b = 0;
			for (j = 0; j < c; j++)
				b += cutscene[i][j];
			// csbrightness[i] = b/c;
			cutscenes++;
			fclose(cutscene_file);
		}
		fclose(cutscene_file);
	}
}


bool CheckSceneHasChanged(void) {
	int		i,j,k;
	int		x;
	int		y;
	int     delta,count;
	int		step;
	long	similar = 0;
static long prevsimilar = 0;
	int		hasBright = 0;
	int		dimCount = 0;
	bool	isDim = false;
	int pixels = 0;
	int		hereBright;
	int		brightCount;
	int		brightCountminX;
	int		brightCountminY;
	int		brightCountmaxX;
	int		brightCountmaxY;
	int		pictureHeight;
	long	cause;
	int		hi;
	double	cur_ar_ratio;
	int  uniform = 0;
	double scale = 1.0;

	if (!width || !height) return (false);
	minY = border;
	maxY = height - border;
	minX = border;
	maxX = width - border;
	brightCount = 0;
	step = 4;
	if (width > 800) step = 8;
	if (width < 400) step = 2;

	memcpy(lastHistogram, histogram, sizeof(histogram));
	last_brightness = brightness;
	brightness = 0;

	// compare current frame with last frame here
	memset(histogram, 0, sizeof(histogram));

#define NEW_AR_ALGO 	
#ifdef NEW_AR_ALGO
	
	delta = 0;
	brightCountminX = 0;
	brightCountminY = 0;
	brightCountmaxX = 0;
	brightCountmaxY = 0;
	while (1) {
		y = border + delta;
		for (x = border + delta; x <= width - border - delta; x += step) {
			if (haslogo[y * width + x])
				continue;
			hereBright = frame_ptr[y * width + x];
			histogram[hereBright]++;
			if (hereBright > test_brightness) 
				brightCountminY++;
		}
		if (brightCountminY < 5) {
			//brightCountminY = 0;
			minY = y;
		}
		y = height - border - delta;
		for (x = border + delta; x <= width - border - delta; x += step) {
			if (haslogo[y * width + x])
				continue;
			hereBright = frame_ptr[y * width + x];
			histogram[hereBright]++;
			if (hereBright > test_brightness) 
				brightCountmaxY++;
		}
		if (brightCountmaxY < 5) {
			//brightCountmaxY = 0;
			maxY = y;
		}
		x = border + delta;
		for (y = border + delta; y <= height - border - delta; y += step) {
			if (haslogo[y * width + x])
				continue;
			hereBright = frame_ptr[y * width + x];
			histogram[hereBright]++;
			if (hereBright > test_brightness) 
				brightCountminX++;
		}
		if (brightCountminX < 5) {
			//brightCountminX = 0;
			minX = x;
		}
		x = width - border - delta;
		if (brightCountmaxX < 5) {
			for (y = border + delta; y <= height - border - delta; y += step) {
				if (haslogo[y * width + x])
					continue;
				hereBright = frame_ptr[y * width + x];
				histogram[hereBright]++;
				if (hereBright > test_brightness) 
					brightCountmaxX++;
			}
			if (brightCountmaxX < 5) {
				//brightCountmaxX = 0;
				maxX = x;
			}
		}
		
		delta += step;
		if (delta > width / 2 || delta > height / 2)
			break;
	}
	
	
#else
	brightCount = 0;
	for (y = border; y < (height - border) / 2; y += step) {
		for (x = lineStart[y]; x <= lineEnd[y]; x += step) {
			hereBright = frame_ptr[y * width + x];
			histogram[hereBright]++;
			if (hereBright > test_brightness) 
				brightCount++;
		}
		if (brightCount < 5) 
			minY = y;
	}

	brightCount = 0;
	for (y = height - border; y >= (height - border) / 2; y -= step) {
		for (x = lineStart[y]; x <= lineEnd[y]; x += step) {
			hereBright = frame_ptr[y * width + x];
			histogram[hereBright]++;
			if (hereBright > test_brightness) 
				brightCount++;
		}
		if (brightCount < 5) 
			maxY = y;
	}
#endif	

#ifdef FRAME_WITH_HISTOGRAM
	if (framearray) memcpy(frame[frame_count].histogram, histogram, sizeof(histogram));
#endif
	if (framearray) frame[frame_count].minY = minY;
	if (framearray) frame[frame_count].maxY = maxY;
	if (framearray) frame[frame_count].minX = minX;
	if (framearray) frame[frame_count].maxX = maxX;

	if (framenum_real <= 1) {

		memcpy(lastHistogram, histogram, sizeof(histogram));
		last_brightness = brightness;


		if (commDetectMethod & AR) {
			ProcessARInfoInit(minY, maxY, minX, maxX);
			if (framearray) frame[frame_count].ar_ratio = last_ar_ratio;
		}

		for (i = max_brightness; i >= 0; i--) last_brightness += histogram[i] * i;
		last_brightness /= (width - (border * 2)) * (height - (border * 2)) / 16;
		if (framearray) {
			frame[frame_count].brightness = last_brightness;
			frame[frame_count].logo_present = false;
			frame[frame_count].schange_percent = 0;
		}

		if (commDetectMethod & SCENE_CHANGE) {
			InitializeSchangeArray(0);
			schange[0].frame = 0;
			schange[0].percentage = 0;
			schange_count++;
		}

		return (false);
	}
	if ( 17652 < frame_count && frame_count < 17657 ) {
//		OutputFrame(frame_count);
	}

	ProcessARInfo(minY, maxY,minX, maxX);
	if (framearray) frame[frame_count].ar_ratio = last_ar_ratio;
	
	similar *= 1;
	
	for (i = 255; i > max_brightness; i--) {
		pixels += histogram[i];
		brightness += histogram[i] * i;
		if (histogram[i]) 
			hasBright++;
//		if (histogram[i] != lastHistogram[i]) similar += abs( histogram[i] - lastHistogram[i]);
		if (histogram[i] < lastHistogram[i]) similar += histogram[i];
		else similar += lastHistogram[i];
	}
	
	for (i = max_brightness; i > test_brightness; i--) {
		pixels += histogram[i];
		brightness += histogram[i] * i;
		dimCount += histogram[i];
//		if (histogram[i] != lastHistogram[i]) similar += abs( histogram[i] - lastHistogram[i]);
		if (histogram[i] < lastHistogram[i]) similar += histogram[i];
		else similar += lastHistogram[i];
	}
	
	for (i = test_brightness; i >= 0; i--) {
		pixels += histogram[i];
		brightness += histogram[i] * i;
//		if (histogram[i] != lastHistogram[i]) similar += abs( histogram[i] - lastHistogram[i]);
		if (histogram[i] < lastHistogram[i]) similar += histogram[i];
		else similar += lastHistogram[i];
	}
	brightness /= pixels;

	if (framearray) frame[frame_count].hasBright = hasBright;
	scale = 720.0 * 480.0 / width / height;
	if (min_hasBright > hasBright * scale) 
		min_hasBright = hasBright * scale;

	if (framearray) frame[frame_count].dimCount = dimCount;
	if (min_dimCount > dimCount * scale) 
		min_dimCount = dimCount * scale;

	if (cutsceneno != 0 || frame_count == cutsceneno)
		RecordCutScene(frame_count, brightness);
	


	uniform = 0;
	for (i = 255; i > brightness + noise_level; i--) {
		uniform +=  histogram[i] * (i - brightness);
	}
	for (i = brightness - noise_level; i >= 0; i--) {
		uniform +=  histogram[i] * (brightness - i);
	}
	uniform = ((double)uniform) * 730/pixels;
	if (framearray) frame[frame_count].uniform = uniform;
	

	x = 0;	
	for (i=10; i<100; i++) {
		if (histogram[i] > 10) {
			if (x<histogram[i])
				x=histogram[i];
			else {
				if (x > histogram[i+1] && histogram[i-1] > 1000) {
					blackHistogram[i-1]++;
					break;
				}
			}
		}
	}
	x = 0;
	for (i=10; i<100; i++) {
		if (blackHistogram[x] < blackHistogram[i]) {
			x = i;
		}
	}
/*	Not tested
	if (x > 10 && (frame_count % 2) == 0) {
		x = x + 5;
		if (x > max_avg_brightness) {
			max_avg_brightness++;
			test_brightness++;
			max_brightness++;
		} else if (x < max_avg_brightness) {
			max_avg_brightness--;
			test_brightness--;
			max_brightness--;
		}
	}
*/
	if (framearray) frame[frame_count].brightness = brightness;
	brightHistogram[brightness]++;
	uniformHistogram[(uniform/UNIFORMSCALE < 255 ? uniform/UNIFORMSCALE : 255)]++;
	if ((dimCount > (int)(.05 * width * height)) && (dimCount < (int)(.35 * width * height))) isDim = true;
	
	sceneChangePercent = (int)(100.0 * similar / pixels);
//	sceneChangePercent = (int)(100.0 * (1.0 - ((float)abs(prevsimilar - similar) / pixels)));
    prevsimilar = similar;
	
	if (framearray) frame[frame_count].schange_percent = sceneChangePercent;


//	cause = ProcessClues(frame_count, brightness, hasBright, isDim, uniform, sceneChangePercent, curvolume, 
//	if (framearray) frame[frame_count].isblack = cause;
//	if (cause != 0)
//		InsertBlackFrame(framenum_real,brightness,uniform,curvolume,cause;

    cause = 0;
	if (commDetectMethod & BLACK_FRAME) {
		if ((brightness <= max_avg_brightness) && hasBright <= maxbright * width * height / 720 / 480 && !isDim /* && uniform < non_uniformity */  /* && !lastLogoTest because logo disappearance is detected too late*/) {
			cause |= C_b;
			Debug(7, "Frame %6i - Black frame with brightness of %i,uniform of %i and volume of %i\n", framenum_real, brightness, uniform, black[black_count-1].volume);
		} else if (non_uniformity > 0) {
			if ((brightness <= max_avg_brightness) && uniform < non_uniformity ) {
				cause |= C_u;
				Debug(7, "Frame %6i - Black frame with brightness of %i,uniform of %i and volume of %i\n", framenum_real, brightness, uniform, black[black_count-1].volume);
			} 
			if (brightness > max_avg_brightness && uniform < non_uniformity && brightness < 180) {
				cause |= C_u;
				Debug(7, "Frame %6i - Uniform frame with brightness of %i and uniform of %i\n", framenum_real, brightness, uniform);
			}
		}	
	}

	if (commDetectMethod & RESOLUTION_CHANGE) {
		if ((old_width != 0 && width != old_width) || (old_height != 0 && height != old_height)) {
			cause |= C_r;
			Debug(7, "Frame %6i - Resolution change from %d x %d to %d x %d \n", framenum_real, old_width, old_height, width, height);
			old_width = width;
			old_height = height;
			ResetLogoBuffers();
		}	
		old_width = width;
		old_height = height;
	}
	
	
/*
	if (abs(brightness - last_brightness) > brightness_jump) {
		cause |= C_s;
		Debug(7, "Frame %6i - Black frame because large brightness change from %i to %i with uniform %i\n", framenum_real, last_brightness, brightness, uniform);
	} // else 
*/	
	if (commDetectMethod & SCENE_CHANGE) {
		
		if (!(frame[frame_count-1].isblack & C_b) && !(cause & C_b)) {
			if (abs(brightness - last_brightness) > brightness_jump) {
				Debug( 7,"Frame %6i - Black frame because large brightness change from %i to %i with uniform %i\n", framenum_real, last_brightness, brightness, uniform);
				cause |= C_s;
			} else if (sceneChangePercent < schange_cutlevel) {
				Debug( 7,"Frame %6i - Black frame because large scene change of %i, uniform %i\n", framenum_real, sceneChangePercent, uniform);

				cause |= C_s;
			}
		}
		
		/*
		if ((sceneChangePercent < 10) && (!hasBright) && !(cause & C_b)) {
		Debug(
		7,
		"Frame %6i - BlackFrame detected because of a nonbright scene change:\tsc - %i\tavg - %i\n",
		framenum_real,
		sceneChangePercent,
		brightness
		);
		cause |= C_s;
		} else if ((sceneChangePercent < 20) && (!hasBright) && !(cause & C_b)) {
		
		  
			
			  
				if (brightness < last_brightness * 2) {
				InitializeSchangeArray(schange_count);
				schange[schange_count].percentage = sceneChangePercent;
				schange[schange_count].frame = framenum_real;
				schange_count++;
				memcpy(lastHistogram, histogram, sizeof(histogram));
				//				Debug(7, "Frame %6i - Scene change with change percentage of %i\n", framenum_real, sceneChangePercent);
				return (true);
				}
				if (0) {
				Debug(
				7,
				"Frame %6i - BlackFrame detected because of scene change with brightness double:\tsc - %i\tavg - %i.................................................................\n",
				framenum_real,
				sceneChangePercent,
				brightness
				);
				cause |= C_s;
				}
		*/
	   
	}
   if (sceneChangePercent < schange_threshold) {
	   // Scene Change threshold: original = 91
	   InitializeSchangeArray(schange_count);
	   schange[schange_count].percentage = sceneChangePercent;
	   schange[schange_count].frame = framenum_real;
	   schange_count++;
//	   memcpy(lastHistogram, histogram, sizeof(histogram));
		   //			Debug(7, "Frame %6i - Scene change with change percentage of %i\n", framenum_real, sceneChangePercent);
   }
	
	for (i=0; i < 255; i++)
		if (histogram[i] > 10) break;
		
	if (brightness < min_brightness_found) min_brightness_found = brightness;

	if (frame_count == 100)
		frame_count = frame_count;

	if (framearray) frame[frame_count].cutscenematch = 100;
//	if (brightness > max_avg_brightness + 10) 
	{
		
		if (framearray) frame[frame_count].cutscenematch = 100;
		for (i = 0; i < cutscenes; i++) {
			if (abs(brightness - csbrightness[i]) < 2) {
				cutscenematch = MatchCutScene(cutscene[i]);
				if (framearray) {
					if (frame[frame_count].cutscenematch > cutscenematch*100/cslength[i])
						frame[frame_count].cutscenematch = cutscenematch*100/cslength[i];
				}
			}
		}	
	}
	if (frame[frame_count].cutscenematch < cutscenedelta)
		cause |= C_t;

	if (cause != 0)
		InsertBlackFrame(framenum_real,brightness,uniform,curvolume,cause);
	if (framearray) frame[frame_count].isblack = cause;

	return (false);
}



// Subroutines for Logo Detection
void PrintLogoFrameGroups(void) {
	int		i,l;
	int		f,t;
	int		count = 0;

	Debug(2, "\nLogos detected on the following frames\n--------------------------------------\n");
	count = 0;
	for (i = 0; i < logo_block_count; i++) {
		f = FindBlock(logo_block[i].start);
		t = FindBlock(logo_block[i].end-2);
		if (f<0) f = 0;
		if (t<0) t = 0;
		if (t < 0) {
			Debug (2, "Panic\n"); 
			break;
		}
		if (f < 0) {
			Debug (2, "Panic\n");
			break;
		}
		Debug(
			2,
			"Logo start - %6i\tend - %6i\tlength - %s\tbefore:%.1f s\t after:%.1f s\n",
			logo_block[i].start,
			logo_block[i].end,
			dblSecondsToStrMinutes((logo_block[i].end - logo_block[i].start) / fps),
			(logo_block[i].start - cblock[f].f_start) / fps,
			(cblock[t].f_end - logo_block[i].end) / fps
			);
		
		count += logo_block[i].end - logo_block[i].start + 1;
		
	}
	for (i = 0; i < logo_block_count-1; i++) {
		f = logo_block[i].end;
		t = logo_block[i+1].start;
		if (max_logo_gap < (t-f)/fps)
			max_logo_gap = (int)((t-f)/fps);
		f = FindBlock(logo_block[i].end);
		t = FindBlock(logo_block[i+1].start);
		for (l = f+1; l < t; l++) {
			if (max_nonlogo_block_length < cblock[l].length)
				max_nonlogo_block_length = cblock[l].length;
		}
	}
	for (i = 0; i < logo_block_count-1; i++) {
		f = FindBlock(logo_block[i].start);
		t = FindBlock(logo_block[i].end);
		if ((logo_block[i].end - logo_block[i].start) > (int)(max_nonlogo_block_length * fps)) {
			l = cblock[f].f_end - logo_block[i].start;
			if (l < cblock[f].length * fps / 10 ) {
				if (l > logo_overshoot )
					logo_overshoot = l;
			}
			l = logo_block[i].end - cblock[t].f_start;
			if (l < cblock[t].length * fps / 10 ) {
				if (l > logo_overshoot)
					logo_overshoot = l;
			}
		}
	}
	if (logo_overshoot > 0)
		logo_overshoot = (int)(logo_overshoot / fps) + 1 + shrink_logo;
	else
		logo_overshoot = shrink_logo;
}

void PrintCCBlocks(void) {
	int i, j;
	Debug(2, "Combining CC Blocks...\n");
	for (i = cc_block_count - 1; i > 0; i--) {
		if ((cc_block[i].end_frame - cc_block[i].start_frame) < fps) {
			Debug(
				4,
				"Removing cc cblock %i because the length is %.2f.\n",
				i,
				(cc_block[i].end_frame - cc_block[i].start_frame) / fps
			);
			for (j = i; j < cc_block_count - 1; j++) {
				cc_block[j].start_frame = cc_block[j + 1].start_frame;
				cc_block[j].end_frame = cc_block[j + 1].end_frame;
				cc_block[j].type = cc_block[j + 1].type;
			}

			cc_block_count--;
		}
	}

	Debug(2, "CC's detected on the following frames - %i total blocks\n--------------------------------------\n", cc_block_count);
	Debug(
		2,
		" 0 - CC start - %6i\tend - %6i\ttype - %s",
		cc_block[0].start_frame,
		cc_block[0].end_frame,
		CCTypeToStr(cc_block[0].type)
	);
	Debug(2, "\tlength - %s\n", dblSecondsToStrMinutes((cc_block[0].end_frame - cc_block[0].start_frame) / fps));
	cc_count[cc_block[0].type] += cc_block[0].end_frame - cc_block[0].start_frame + 1;

	for (i = 1; i < cc_block_count; i++) {
		Debug(
			2,
			"%2i - CC start - %6i\tend - %6i\ttype - %s",
			i,
			cc_block[i].start_frame,
			cc_block[i].end_frame,
			CCTypeToStr(cc_block[i].type)
		);
		Debug(2, "\tlength - %s\n", dblSecondsToStrMinutes((cc_block[i].end_frame - cc_block[i].start_frame) / fps));
		cc_count[cc_block[i].type] += cc_block[i].end_frame - cc_block[i].start_frame + 1;
	}

	Debug(2, "\nCaption sums\n---------------------------\n");
	Debug(
		2,
		"Pop on captions:   %6i:%5.2f - %s\n",
		cc_count[POPON],
		((double)cc_count[POPON] / (double)framesprocessed) * 100.0,
		dblSecondsToStrMinutes(cc_count[POPON] / fps)
	);
	Debug(
		2,
		"Roll up captions:  %6i:%5.2f - %s\n",
		cc_count[ROLLUP],
		((double)cc_count[ROLLUP] / (double)framesprocessed) * 100.0,
		dblSecondsToStrMinutes(cc_count[ROLLUP] / fps)
	);
	Debug(
		2,
		"Paint on captions: %6i:%5.2f - %s\n",
		cc_count[PAINTON],
		((double)cc_count[PAINTON] / (double)framesprocessed) * 100.0,
		dblSecondsToStrMinutes(cc_count[PAINTON] / fps)
	);
	Debug(
		2,
		"No captions:       %6i:%5.2f - %s\n",
		cc_count[NONE],
		((double)cc_count[NONE] / (double)framesprocessed) * 100.0,
		dblSecondsToStrMinutes(cc_count[NONE] / fps)
	);
	for (i = 0; i <= 4; i++) {
		if (cc_count[i] > cc_count[most_cc_type]) {
			most_cc_type = i;
		}
	}

	Debug(2, "The %s type of closed captions were determined to be the most common.\n", CCTypeToStr(most_cc_type));
}

/*
static edge_inc = 1;
static edge_dec = 20;


void EdgeCount(unsigned char* frame_ptr) {
	int				i,index;
	int				x;
	int				y;
	unsigned char	herePixel;
	static int framecnt;

	edge_count = 0;
	if (aggressive_logo_rejection) {
		for (y = edge_radius + (int)(height * borderIgnore); y < (subtitles? height/2 : (height - edge_radius - (int)(height * borderIgnore))); y++) {
			for (x = edge_radius + (int)(width * borderIgnore); x < (width - edge_radius - (int)(width * borderIgnore)); x++) {
				herePixel = frame_ptr[y * width + x];
				if (
					(abs(frame_ptr[y * width + (x - edge_radius)] - herePixel) >= edge_level_threshold)
					) {
					if (hor_edgecount[y * width + x] <= num_logo_buffers) 
						hor_edgecount[y * width + x]++;
					else
						edge_count++;
				} else 
					hor_edgecount[y * width + x] = 0;
				
				if (
					(abs(frame_ptr[(y - edge_radius) * width + x] - herePixel) >= edge_level_threshold)
					) {
					if (ver_edgecount[y * width + x] <= num_logo_buffers) 
						ver_edgecount[y * width + x]++;
					else
						edge_count++;
				} else 
					ver_edgecount[y * width + x] = 0;
			}
		}
	} else { 
		for (y = edge_radius + (int)(height * borderIgnore); y < (subtitles? height/2 : (height - edge_radius - (int)(height * borderIgnore))); y++) {
			for (x = edge_radius + (int)(width * borderIgnore); x < (width - edge_radius - (int)(width * borderIgnore)); x++) {
				herePixel = frame_ptr[y * width + x];
				if (
					(abs(frame_ptr[y * width + (x - edge_radius)] - herePixel) >= edge_level_threshold) ||
					(abs(frame_ptr[y * width + (x + edge_radius)] - herePixel) >= edge_level_threshold)
					) {
					if (hor_edgecount[y * width + x] < num_logo_buffers) 
						hor_edgecount[y * width + x]++;
					else
						edge_count++;
				} else 
					hor_edgecount[y * width + x] = 0;
				
				if (
					(abs(frame_ptr[(y - edge_radius) * width + x] - herePixel) >= edge_level_threshold) ||
					(abs(frame_ptr[(y + edge_radius) * width + x] - herePixel) >= edge_level_threshold)
					) {
					if (ver_edgecount[y * width + x] < num_logo_buffers) 
						ver_edgecount[y * width + x]++;
					else
						edge_count++;
				} else 
					ver_edgecount[y * width + x] = 0;
			}
		}
	}
	if (edge_count > 350) 
		logoBuffersFull = true;
}

*/

#define TEST_HEDGE1(FRAME,X,Y)	(abs(FRAME[(Y) * width + (X) - edge_radius]   - FRAME[(Y) * width + (X) + edge_radius]  ) >= edge_level_threshold)
#define TEST_VEDGE1(FRAME,X,Y)	(abs(FRAME[((Y) - edge_radius) * width + (X)] - FRAME[((Y) + edge_radius) * width + (X)]) >= edge_level_threshold)

#define TEST_HEDGE0(FRAME,X,Y)  (abs(FRAME[(Y) * width + (X) - edge_radius]   - FRAME[(Y) * width + (X)]  ) >= edge_level_threshold) || \
								(abs(FRAME[(Y) * width + (X) + edge_radius]   - FRAME[(Y) * width + (X)]  ) >= edge_level_threshold)

#define TEST_VEDGE0(FRAME,X,Y)	(abs(FRAME[((Y) - edge_radius) * width + (X)] - FRAME[((Y)) * width + (X)]) >= edge_level_threshold) || \
								(abs(FRAME[((Y) + edge_radius) * width + (X)] - FRAME[((Y)) * width + (X)]) >= edge_level_threshold)

#define TEST_HEDGE2(FRAME,X,Y)  (abs((FRAME[(Y) * width + (X) - edge_radius - 1] + FRAME[(Y) * width + (X) - edge_radius] + FRAME[(Y) * width + (X) - edge_radius + 1]) - \
									 (FRAME[(Y) * width + (X) + edge_radius - 1] + FRAME[(Y) * width + (X) + edge_radius] + FRAME[(Y) * width + (X) + edge_radius + 1])   )/3 >= edge_level_threshold)

#define TEST_VEDGE2(FRAME,X,Y)	(abs((FRAME[((Y) - edge_radius - 1) * width + (X)] + FRAME[((Y) - edge_radius) * width + (X)] + FRAME[((Y) - edge_radius + 1) * width + (X)]) - \
									 (FRAME[((Y) + edge_radius - 1) * width + (X)] + FRAME[((Y) + edge_radius) * width + (X)] + FRAME[((Y) + edge_radius + 1) * width + (X)])   )/3 >= edge_level_threshold)


#define TEST_HEDGE3(FRAME,X,Y)	(abs((\
FRAME[((Y)-edge_radius)*width+(X)-edge_radius]-FRAME[((Y)-edge_radius)*width+(X)+edge_radius] +\
FRAME[((Y)            )*width+(X)-edge_radius]-FRAME[((Y)            )*width+(X)+edge_radius] +\
FRAME[((Y)+edge_radius)*width+(X)-edge_radius]-FRAME[((Y)+edge_radius)*width+(X)+edge_radius])\
) >= edge_level_threshold)

#define TEST_VEDGE3(FRAME,X,Y)	(abs((\
FRAME[((Y)-edge_radius)*width+(X)-edge_radius]-FRAME[((Y)+edge_radius)*width+(X)-edge_radius] +\
FRAME[((Y)-edge_radius)*width+(X)            ]-FRAME[((Y)+edge_radius)*width+(X)            ] +\
FRAME[((Y)-edge_radius)*width+(X)+edge_radius]-FRAME[((Y)+edge_radius)*width+(X)+edge_radius])\
) >= edge_level_threshold)


#define AR_DIST	20

#define LOGO_Y_LOOP	for (y = (logo_at_bottom ? height/2 : edge_radius + (int)(height * borderIgnore)); y < (subtitles? height/2 : (height - edge_radius - (int)(height * borderIgnore))); y += edge_step)
// #define LOGO_X_LOOP for (x = max(edge_radius + (int)(width * borderIgnore), minX+AR_DIST); x < min((width - edge_radius - (int)(width * borderIgnore)),maxX-AR_DIST); x += edge_step) 
#define LOGO_X_LOOP for (x = edge_radius + (int)(width * borderIgnore); x < (width - edge_radius - (int)(width * borderIgnore)); x += edge_step) 

void EdgeDetect(unsigned char* frame_ptr, int maskNumber) {
	int				i;
	int				x;
	int				y;
	//	unsigned char	temp[MAXWIDTH * MAXHEIGHT];
	unsigned char	herePixel;
//	memset(for (i = 0; i <= (width * height); i++) temp[i] = 0;
	hedge_count = 0;
	vedge_count = 0;
#ifdef MAXMIN_LOGO_SEARCH
	if (maskNumber == 0) {
		memset(max_br, 0, sizeof(max_br));
		memset(min_br, 255, sizeof(max_br));
	}
	for (y = (logo_at_bottom ? height/2 : edge_radius + (int)(height * borderIgnore)); y < (subtitles? height/2 : (height - edge_radius - (int)(height * borderIgnore))); y++) {
		for (x = max(edge_radius + (int)(width * borderIgnore), minX+AR_DIST); x < min((width - edge_radius - (int)(width * borderIgnore)),maxX-AR_DIST); x++) {
			herePixel = frame_ptr[y * width + x];
			if (herePixel < min_br[y * width + x])
				min_br[y * width + x] = herePixel;
			if (herePixel > max_br[y * width + x])
				max_br[y * width + x] = herePixel;
		}
	}
#endif
#if MULTI_EDGE_BUFFER
	memset(horiz_edges[maskNumber], 0, width * height);
	memset(vert_edges[maskNumber], 0, width * height);
	for (y = (logo_at_bottom ? height/2 : edge_radius + (int)(height * borderIgnore)); y < (subtitles? height/2 : (height - edge_radius - (int)(height * borderIgnore))); y++) {
		for (x = max(edge_radius + (int)(width * borderIgnore), minX+AR_DIST); x < min((width - edge_radius - (int)(width * borderIgnore)),maxX-AR_DIST); x++) {
			herePixel = frame_ptr[y * width + x];
			if ((abs(frame_ptr[y * width + (x - edge_radius)] - herePixel) >= edge_level_threshold) ||
				(abs(frame_ptr[y * width + (x + edge_radius)] - herePixel) >= edge_level_threshold)) {
				horiz_edges[maskNumber][y * width + x] = 1;
			}

			if ((abs(frame_ptr[(y - edge_radius) * width + x] - herePixel) >= edge_level_threshold) ||
				(abs(frame_ptr[(y + edge_radius) * width + x] - herePixel) >= edge_level_threshold)) {
				vert_edges[maskNumber][y * width + x] = 1;
			}
		}
	}
#else
	if (aggressive_logo_rejection==1) {
		LOGO_X_LOOP {
			LOGO_Y_LOOP {
					if (TEST_HEDGE1(frame_ptr,x,y)) {
						if (hor_edgecount[y * width + x] < num_logo_buffers) 
							hor_edgecount[y * width + x]++;
						else
							edge_count++;
					} else 
						hor_edgecount[y * width + x] = 0;
					if (TEST_VEDGE1(frame_ptr,x,y)) {
						if (ver_edgecount[y * width + x] < num_logo_buffers) 
							ver_edgecount[y * width + x]++;
						else
							edge_count++;
					} else 
						ver_edgecount[y * width + x] = 0;
			}
		}
	} else if (aggressive_logo_rejection==2) {
		LOGO_X_LOOP {
			LOGO_Y_LOOP {
				if (TEST_HEDGE2(frame_ptr,x,y)) {
					if (hor_edgecount[y * width + x] < num_logo_buffers) 
						hor_edgecount[y * width + x]++;
					else
						edge_count++;
				} else 
					hor_edgecount[y * width + x] = 0;
				if (TEST_VEDGE2(frame_ptr,x,y)) {
					if (ver_edgecount[y * width + x] < num_logo_buffers) 
						ver_edgecount[y * width + x]++;
					else
						edge_count++;
				} else 
					ver_edgecount[y * width + x] = 0;
			}
		}
//	printf("%6d %6d\n", hedge_count, vedge_count);
	} else if (aggressive_logo_rejection==3) {
		LOGO_X_LOOP {
			LOGO_Y_LOOP {
				if (TEST_HEDGE3(frame_ptr,x,y)) {
					if (hor_edgecount[y * width + x] < num_logo_buffers) 
						hor_edgecount[y * width + x]++;
					else
						edge_count++;
				} else 
					hor_edgecount[y * width + x] = 0;
				if (TEST_VEDGE3(frame_ptr,x,y)) {
					if (ver_edgecount[y * width + x] < num_logo_buffers) 
						ver_edgecount[y * width + x]++;
					else
						edge_count++;
				} else 
					ver_edgecount[y * width + x] = 0;
			}
		}
	} else { 
		LOGO_X_LOOP {
			LOGO_Y_LOOP {
				if ((/*frame_ptr[y * width + x - edge_radius] > 50 && */ frame_ptr[y * width + x - edge_radius] < 200) || ( /*frame_ptr[y * width + x + edge_radius] > 50 && */ frame_ptr[y * width + x + edge_radius] < 200) ) {
					if (TEST_HEDGE0(frame_ptr,x,y)) {
						if (hor_edgecount[y * width + x] < num_logo_buffers) 
							hor_edgecount[y * width + x]++;
						else
							edge_count++;
					} else 
						hor_edgecount[y * width + x] = 0;
				}
				if ((/*frame_ptr[(y- edge_radius) * width + x ] > 50 && */ frame_ptr[(y- edge_radius) * width + x ] < 200) || ( /*frame_ptr[(y+ edge_radius) * width + x ] > 50 && */ frame_ptr[(y+ edge_radius) * width + x ] < 200) ) {
					if (TEST_VEDGE0(frame_ptr,x,y)) {
						if (ver_edgecount[y * width + x] < num_logo_buffers) 
							ver_edgecount[y * width + x]++;
						else
							edge_count++;
					} else 
						ver_edgecount[y * width + x] = 0;
				}
			}
		}
	}
#endif
}



double CheckStationLogoEdge(unsigned char* testFrame) {
	int		index;
	int		x;
	int		y;
	int		herePixel;
	int		testEdges = 0;
	int		testNotEdges = 0;

	int goodEdges = 0;

	currentGoodEdge = 0.0;
	if (aggressive_logo_rejection == 1) {
		for (y = logoMinY; y <= logoMaxY; y += edge_step) {
			for (x = logoMinX; x <= logoMaxX; x += edge_step) {
				index = y * width + x;
				if (horiz_edgemask[index]) {
					if (TEST_HEDGE1(testFrame,x,y)) {
						goodEdges++;
					}
					testEdges++;
				}
				if (vert_edgemask[index]) {
					if (TEST_VEDGE1(testFrame,x,y)) {
						goodEdges++;
					}
					
					testEdges++;
				} 
			}
		}
	} else if (aggressive_logo_rejection == 2) {
		for (y = logoMinY; y <= logoMaxY; y += edge_step) {
			for (x = logoMinX; x <= logoMaxX; x += edge_step) {
				index = y * width + x;
				if (horiz_edgemask[index]) {
					if (TEST_HEDGE2(testFrame,x,y)) {
						goodEdges++;
					}
					testEdges++;
				}
				if (vert_edgemask[index]) {
					if (TEST_VEDGE2(testFrame,x,y)) {
						goodEdges++;
					}
					
					testEdges++;
				} 
			}
		}
	} else if (aggressive_logo_rejection == 3) {
		for (y = logoMinY; y <= logoMaxY; y += edge_step) {
			for (x = logoMinX; x <= logoMaxX; x += edge_step) {
				index = y * width + x;
				if (horiz_edgemask[index]) {
					if (TEST_HEDGE3(testFrame,x,y)) {
						goodEdges++;
					}
					testEdges++;
				}
				if (vert_edgemask[index]) {
					if (TEST_VEDGE3(testFrame,x,y)) {
						goodEdges++;
					}
					
					testEdges++;
				} 
			}
		}
	} else {
		for (y = logoMinY; y <= logoMaxY; y += edge_step) {
			for (x = logoMinX; x <= logoMaxX; x += edge_step) {
				index = y * width + x;
				if (horiz_edgemask[index]) {
					if (TEST_HEDGE0(testFrame,x,y)) {
						goodEdges++;
					}
					testEdges++;
				}
				if (vert_edgemask[index]) {
					if (TEST_VEDGE0(testFrame,x,y)) {
						goodEdges++;
					}
					
					testEdges++;
				} 
			}
		}
	}
	return (((double)goodEdges / (double)testEdges));
}

void InitProcessLogoTest()
{
	logo_block_count = 0;
	logoTrendCounter = 0;
	frames_with_logo = 0;
	lastLogoTest = false;
	curLogoTest = false;
}


#define LOGO_SAMPLE (int)(fps * logoFreq)

bool ProcessLogoTest(int framenum_real, int curLogoTest, int close)
{

	
	
	int i;
	double s1,s2;

	if (logo_filter > 0) {
		if (!close) {
			
			if (framenum_real > logo_filter * 2 * LOGO_SAMPLE) {
				s1 = s2 = 0.0;
				for ( i = 0; i < logo_filter; i++) {
					s1 += (frame[framenum_real - i * LOGO_SAMPLE - logo_filter * LOGO_SAMPLE].currentGoodEdge - logo_threshold > 0 ? 1 : -1);
					s2 += (frame[framenum_real - i * LOGO_SAMPLE].currentGoodEdge - logo_threshold > 0 ? 1 : -1);
				}
				s1 /= logo_filter;
				s2 /= logo_filter;
				for (i = 0; i < LOGO_SAMPLE; i++) {
					frame[framenum_real - logo_filter * LOGO_SAMPLE - i].logo_filter = (s1 + s2);
				}
			}
			for (i = 0; i < LOGO_SAMPLE; i++) {
				frame[framenum_real - i].logo_filter = 0.0;
			}
			
			framenum_real -= logo_filter*LOGO_SAMPLE;
			if (framenum_real < 0) framenum_real= 1;
			
			curLogoTest = (frame[framenum_real].logo_filter > 0.0 ? 1 : 0);
		}
		else
			curLogoTest = false;
	}
	
	if (curLogoTest != lastLogoTest) {
		if (!curLogoTest) {
			// Logo disappeared
			lastLogoTest = false;
			logoTrendCounter = 0;
			logo_block[logo_block_count].end = framenum_real - 1 * (int)(fps * logoFreq);
			if (logo_block[logo_block_count].end - logo_block[logo_block_count].start > 
				2*(int)(shrink_logo*fps) ) {
				logo_block[logo_block_count].end -= (int)(shrink_logo*fps); 
				logo_block[logo_block_count].start += (int)(shrink_logo*fps);
				frames_with_logo -= 2 * (int)(fps * logoFreq) + 2*(int)(shrink_logo*fps);
				if (framearray) {
					for (i = logo_block[logo_block_count].end; i < framenum_real; i++)
						frame[i].logo_present = false;
				}
				Debug
					(3,
					"\nEnd logo cblock %i\tframe %i\tLength - %s\n",
					logo_block_count,
					logo_block[logo_block_count].end,
					dblSecondsToStrMinutes(((logo_block[logo_block_count].end - logo_block[logo_block_count].start) / fps))
					);
				logo_block_count++;
			} else { 
				logo_block[logo_block_count].start = -1; // else discard logo cblock
			} 
		} else {
			// real change or false change?
			logoTrendCounter++;
			if (logoTrendCounter == minHitsForTrend) {
				lastLogoTest = true;
				logoTrendCounter = 0;
				InitializeLogoBlockArray(logo_block_count + 2);
				logo_block[logo_block_count + 1].start = -1;
				logo_block[logo_block_count].start = framenum_real - ((int)(fps * logoFreq) * (minHitsForTrend - 1));
				frames_with_logo +=((int)(fps * logoFreq) * (minHitsForTrend - 1));
				if (framearray) {
					for (i = logo_block[logo_block_count].start; i < framenum_real; i++)
						frame[i].logo_present = true;
				}
				if (!logo_block_count) {
					Debug(
						3,
						"\t\t\t\tStart logo cblock %i\tframe %i\n",
						logo_block_count,
						logo_block[logo_block_count].start
						);
				} else {
					Debug(
						3,
						"\n\t\t\t\tNonlogo Length - %s\nStart logo cblock %i\tframe %i\n",
						dblSecondsToStrMinutes(((logo_block[logo_block_count].start - logo_block[logo_block_count - 1].end) / fps)),
						logo_block_count,
						logo_block[logo_block_count].start
						);
				}
				
			}
		} 
	} else {
		logoTrendCounter = 0;
	}	

	
	return(lastLogoTest);
}


void ResetLogoBuffers(void) {
	int i;
	newestLogoBuffer = oldestLogoBuffer = 0;
	if (newestLogoBuffer == num_logo_buffers) newestLogoBuffer = 0; // rotates buffer
	logoFrameNum[newestLogoBuffer] = framenum_real;
	oldestLogoBuffer = 0;
	for (i = 0; i < num_logo_buffers; i++) {
		free(logoFrameBuffer[i]);
	}
	for (i = 0; i < num_logo_buffers; i++) {
		logoFrameBuffer[i] = malloc(width * height * sizeof(frame_ptr[0]));
		if (logoFrameBuffer[i] == NULL) {
			Debug(0, "Could not allocate memory for logo frame buffer %i\n", i);
			exit(16);
		}
	}
}

void FillLogoBuffer(void) {
	int i;
	int x;
	int y;
	newestLogoBuffer++;
	if (newestLogoBuffer == num_logo_buffers) newestLogoBuffer = 0; // rotates buffer
	logoFrameNum[newestLogoBuffer] = framenum_real;
	oldestLogoBuffer = 0;
	for (i = 0; i < num_logo_buffers; i++) {
		if (logoFrameNum[i]  && logoFrameNum[i] < logoFrameNum[oldestLogoBuffer]) oldestLogoBuffer = i;
	}

	i = min(logoFrameBufferSize, width * height * sizeof(frame_ptr[0]));
	memcpy(logoFrameBuffer[newestLogoBuffer], frame_ptr, i);

//	for (y = 0; y < height; y++) {
//		for (x = 0; x < width; x++) {
//			logoFrameBuffer[newestLogoBuffer][y * width + x] = frame_ptr[y * width + x];
//		}
//	}

	EdgeDetect(logoFrameBuffer[newestLogoBuffer], newestLogoBuffer);
	if ((!logoBuffersFull) && (newestLogoBuffer == num_logo_buffers - 1)) logoBuffersFull = true;
}

bool SearchForLogoEdges(void) {
	int		i;
	int		x;
	int		maxY,minY;
	int		y;
	double	logoPercentageOfScreen;
	bool	LogoIsThere;
	int		sum;
	int		tempMinX;
	int		tempMaxX;
	int		tempMinY;
	int		tempMaxY;
	int		excludeUnderX = width / 2;
	int		index;
	logoMinX = edge_radius;
	logoMaxX = width - edge_radius;
	logoMinY = edge_radius;
	logoMaxY = height - edge_radius;
#if MULTI_EDGE_BUFFER
	memset(horiz_edgemask, 1, width * height);
	memset(vert_edgemask, 1, width * height);
	for (i = 0; i < 1; i++) {
		for (y = border; y < height - border; y++) {
			for (x = border; x < width - border; x++) {
				if (!horiz_edgemask[y * width + x] || !horiz_edges[i][y * width + x]) {
					horiz_edgemask[y * width + x] = 0;
				}

				if (!vert_edgemask[y * width + x] || !vert_edges[i][y * width + x]) {
					vert_edgemask[y * width + x] = 0;
				}
			}
		}
	}
#if 0
	for (y = border; y < height - border; y++) {
		for (x = border; x < width - border; x++) {
			index = y * width + x;
			for (i = 1; i < num_logo_buffers; i++) {
				if (!horiz_edgemask[index] || !horiz_edges[i][index]) {
					horiz_edgemask[index] = 0;
					break;
				}
			}
			for (i = 1; i < num_logo_buffers; i++) {
				if (!vert_edgemask[index] || !vert_edges[i][index]) {
					vert_edgemask[index] = 0;
					break;
				}
			}
		}
	}
#else
	for (i = 1; i < num_logo_buffers; i++) {
		for (y = border; y < height - border; y++) {
			for (x = border; x < width - border; x++) {
				if (!horiz_edgemask[y * width + x] || !horiz_edges[i][y * width + x]) {
					horiz_edgemask[y * width + x] = 0;
				}

				if (!vert_edgemask[y * width + x] || !vert_edges[i][y * width + x]) {
					vert_edgemask[y * width + x] = 0;
				}
			}
		}
	}
#endif
#else
	memset(horiz_edgemask, 0, width * height);
	memset(vert_edgemask, 0, width * height);
//	minY = (logo_at_bottom ? height/2 : edge_radius + (int)(height * borderIgnore));
//	if (framearray) minY = max(minY, frame[frame_count].minY);
//	maxY = (subtitles? height/2 : height - edge_radius - (int)(height * borderIgnore));
//	if (framearray) maxY = min(maxY, frame[frame_count].maxY);

	LOGO_X_LOOP {
		LOGO_Y_LOOP {	
//	for (y = minY; y < maxY; y++) {
//		for (x = edge_radius + (int)(width * borderIgnore); x < width - edge_radius + (int)(width * borderIgnore); x++) {
			if (hor_edgecount[y * width + x]>= num_logo_buffers) {
				horiz_edgemask[y * width + x] = 1;
			}
			if (ver_edgecount[y * width + x]>= num_logo_buffers) {
				vert_edgemask[y * width + x] = 1;
			}
		}
	}
#endif

	ClearEdgeMaskArea(horiz_edgemask, vert_edgemask);
	ClearEdgeMaskArea(vert_edgemask, horiz_edgemask);


	SetEdgeMaskArea(horiz_edgemask);
	tempMinX = logoMinX;
	tempMaxX = logoMaxX;
	tempMinY = logoMinY;
	tempMaxY = logoMaxY;
	logoMinX = 2;
	logoMaxX = width - 2;
	logoMinY = 2;
	logoMaxY = height - 2;
	SetEdgeMaskArea(vert_edgemask);
	if (tempMinX < logoMinX) logoMinX = tempMinX;
	if (tempMaxX > logoMaxX) logoMaxX = tempMaxX;
	if (tempMinY < logoMinY) logoMinY = tempMinY;
	if (tempMaxY > logoMaxY) logoMaxY = tempMaxY;
	logoPercentageOfScreen = (double)((logoMaxY - logoMinY) * (logoMaxX - logoMinX)) / (double)(height * width);
	if (logoPercentageOfScreen > logo_max_percentage_of_screen) {
//			Debug(
//				3,
//				"Reducing logo search area!\tPercentage of screen - %.2f%% TOO BIG.\n",
//				logoPercentageOfScreen * 100
//			);

		if (tempMinX > logoMinX+50) logoMinX = tempMinX;
		if (tempMaxX < logoMaxX-50) logoMaxX = tempMaxX;
		if (tempMinY > logoMinY+50) logoMinY = tempMinY;
		if (tempMaxY < logoMaxY-50) logoMaxY = tempMaxY;
	}

	i = CountEdgePixels();
//printf("Edges=%d\n",i);
	if (i > 350) {
		logoPercentageOfScreen = (double)((logoMaxY - logoMinY) * (logoMaxX - logoMinX)) / (double)(height * width);
		if (i > 40000 || logoPercentageOfScreen > logo_max_percentage_of_screen) {
			Debug(
				3,
				"Edge count - %i\tPercentage of screen - %.2f%% TOO BIG, CAN'T BE A LOGO.\n",
				i,
				logoPercentageOfScreen * 100
			);
			logoInfoAvailable = false;
		} else {
			Debug(3, "Edge count - %i\tPercentage of screen - %.2f%%, Check: %i\n", i, logoPercentageOfScreen * 100,doublCheckLogoCount);
			logoInfoAvailable = true;
		}
	}

	if (logoInfoAvailable) {
		doublCheckLogoCount++;
		if (doublCheckLogoCount > 5) {
			// Final check done, found
		}
		else
			logoInfoAvailable = false;
	} else {
		doublCheckLogoCount = 0;
	}


	sum = 0;
	oldestLogoBuffer = 0;
	for (i = 0; i < num_logo_buffers; i++) {
		if (logoFrameNum[i]  && logoFrameNum[i] < logoFrameNum[oldestLogoBuffer]) oldestLogoBuffer = i;
	}

	if (logoInfoAvailable) {
		Debug(3, "Doublechecking frames %i to %i for logo.\n", logoFrameNum[oldestLogoBuffer], logoFrameNum[newestLogoBuffer]);
		for (i = 0; i < num_logo_buffers; i++) {
			currentGoodEdge = CheckStationLogoEdge(logoFrameBuffer[i]);
			LogoIsThere = (currentGoodEdge > logo_threshold);

			if (LogoIsThere) {
				Debug(7, "Logo present in frame %i.\n", logoFrameNum[i]);
				sum++;
			} else {
				Debug(7, "Logo not present in frame %i.\n", logoFrameNum[i]);
			}
		}
	}


	if (logoInfoAvailable && (sum >= (int)(num_logo_buffers * .9))) {
		logo_block[0].start = logoFrameNum[oldestLogoBuffer];
		DumpEdgeMasks();
//		DumpEdgeMask(horiz_edgemask, HORIZ);
//		DumpEdgeMask(vert_edgemask, VERT);
		for (i = 0; i < num_logo_buffers; i++) {
#if MULTI_EDGE_BUFFER
			free(vert_edges[i]);
			free(horiz_edges[i]);
#endif
			free(logoFrameBuffer[i]);
		}
#if MULTI_EDGE_BUFFER
		free(vert_edges);
		vert_edges = NULL
		free(horiz_edges);
		horiz_edges = NULL;
#else
//		free(horiz_count);
//		horiz_count = NULL;
//		free(vert_count);
//		vert_count = NULL;
#endif
		free(logoFrameBuffer);
		logoFrameBuffer = NULL;
		InitScanLines();
		InitHasLogo();
	
	} else {
		logoInfoAvailable = false;
	}

	if (!logoInfoAvailable && (framenum_real > (int)(giveUpOnLogoSearch * fps))) {
		Debug(1, "No logo was found after %i frames.\nGiving up", framenum_real);
		commDetectMethod -= LOGO;
	}
	if (added_recording > 0) 
		giveUpOnLogoSearch += added_recording * 60;

	if (logoInfoAvailable && startOverAfterLogoInfoAvail) {
		Debug(3, "logoMinX=%i\tlogoMaxX=%i\tlogoMinY=%i\tlogoMaxY=%i\n", logoMinX, logoMaxX, logoMinY, logoMaxY);
		SaveLogoMaskData();
		Debug(3, "******************* End of Logo Processing ***************\n");
		return false;		
	}

	return true;
}


#define MAX_SEARCH 25

int ClearEdgeMaskArea(unsigned char* temp, unsigned char* test) {
	int x;
	int y;
	int count;
	int valid = 0;
	int offset;
	int ix,iy;

	for (y = (logo_at_bottom ? height/2 : border); y < (subtitles? height/2 : height - border); y++) {
		for (x = border; x < width - border; x++) {
			count = 0;
			if (temp[y * width + x] == 1) {
				if (test[y * width + x] == 1) 
//					goto found;
					count++;
				
				for (offset = edge_step; offset < MAX_SEARCH; offset += edge_step) {
					iy = offset;	
					for (ix= -offset; ix <= offset; ix += edge_step)
						if (y+iy > 0 && y+iy<height && x+ix > 0 && x+ix < width && test[(y+iy) * width + x+ix] == 1) 
//							goto found;
							count++;

					iy = -offset;	
					for (ix= -offset; ix <= offset; ix += edge_step)
						if (y+iy > 0 && y+iy<height && x+ix > 0 && x+ix < width && test[(y+iy) * width + x+ix] == 1) 
//							goto found;
							count++;
					ix = offset;
					for (iy= -offset+edge_step; iy <= offset-edge_step; iy += edge_step)
						if (y+iy > 0 && y+iy<height && x+ix > 0 && x+ix < width && test[(y+iy) * width + x+ix] == 1) 
//							goto found;
							count++;
					ix = -offset;
					for (iy= -offset+edge_step; iy <= offset-edge_step; iy += edge_step)
						if (y+iy > 0 && y+iy<height && x+ix > 0 && x+ix < width && test[(y+iy) * width + x+ix] == 1) 
//							goto found;
							count++;
					if (count >= edge_weight)
						goto found;
				}
				temp[y * width + x] = 0;
				continue;
found:			valid++;
			}
		}
	}
	return(valid);
}

#define LOGOBORDER	4

void SetEdgeMaskArea(unsigned char* temp) {
	int x;
	int y;
	logoMinX = width - 1;
	logoMaxX = 0;
	logoMinY = height - 1;
	logoMaxY = 0;
	for (y = (logo_at_bottom ? height/2 : border); y < (subtitles? height/2 : height - border); y++) {
		for (x = border; x < width - border; x++) {
			if (temp[y * width + x] == 1) {
				if (x - LOGOBORDER < logoMinX) logoMinX = x - LOGOBORDER;
				if (y - LOGOBORDER < logoMinY) logoMinY = y - LOGOBORDER;
				if (x + LOGOBORDER > logoMaxX) logoMaxX = x + LOGOBORDER;
				if (y + LOGOBORDER > logoMaxY) logoMaxY = y + LOGOBORDER;
			}
		}
	}

	if (logoMinX < edge_radius) logoMinX = edge_radius;
	if (logoMaxX > (width - edge_radius)) logoMaxX = (width - edge_radius);
	if (logoMinY < edge_radius) logoMinY = edge_radius;
	if (logoMaxY > (height - edge_radius)) logoMaxY = (height - edge_radius);
}

int CountEdgePixels(void) {
	int x;
	int y;
	int count = 0;
	int hcount = 0;
	int vcount = 0;
	for (y = logoMinY; y <= logoMaxY; y++) {
		for (x = logoMinX; x <= logoMaxX; x++) {
			if (horiz_edgemask[y * width + x]) hcount++;
			if (vert_edgemask[y * width + x]) vcount++;
		}
	}

	count = hcount + vcount;
//	printf("%6d %6d\n",hcount, vcount);
	if ((hcount < 50) || (vcount < 50)) count = 0;
	return (count);
}

void DumpEdgeMask(unsigned char* buffer, int direction) {
	int x;
	int y;
	char outbuf[MAXWIDTH+1];
	int i;
	switch (direction) {
	case HORIZ:
		Debug(1, "\nHorizontal Logo Mask \n     ");
		break;

	case VERT:
		Debug(1, "\nVertical Logo Mask \n     ");
		break;

	case DIAG1:
		Debug(1, "\nDiagonal 1 Logo Mask \n     ");
		break;

	case DIAG2:
		Debug(1, "\nDiagonal 2 Logo Mask \n     ");
		break;
	}

	for (x = logoMinX; x <= logoMaxX; x++) {
		outbuf[x-logoMinX] = '0'+ (x % 10);
	}
	outbuf[x-logoMinX] = 0;
	Debug(1, "%s\n",outbuf);
	

	Debug(1, "\n");
	for (y = logoMinY; y <= logoMaxY; y++) {
		Debug(1, "%3d: ", y);
		for (x = logoMinX; x <= logoMaxX; x++) {
			switch (buffer[y * width + x]) {
			case 0:
				outbuf[x-logoMinX] = ' ';
				break;

			case 1:
				outbuf[x-logoMinX] = '*';
				break;
			}
		}
		outbuf[x-logoMinX] = 0;
		Debug(1, "%s\n",outbuf);
		
	}
}

void DumpEdgeMasks(void) {
	int x;
	int y;
	char outbuf[MAXWIDTH+1];
	int i;

	for (x = logoMinX; x <= logoMaxX; x++) {
		outbuf[x-logoMinX] = '0'+ (x % 10);
	}
	outbuf[x-logoMinX] = 0;
	Debug(1, "%s\n",outbuf);

	for (y = logoMinY; y <= logoMaxY; y++) {
		Debug(1, "%3d: ", y);
		for (x = logoMinX; x <= logoMaxX; x++) {
			switch (horiz_edgemask[y * width + x]) {
			case 0:
				if (vert_edgemask[y * width + x] == 1) 
					outbuf[x-logoMinX] =  '-';
				else
					outbuf[x-logoMinX] =  ' ';
				break;

			case 1:
				if (vert_edgemask[y * width + x] == 1) 
					outbuf[x-logoMinX] =  '+';
				else
					outbuf[x-logoMinX] =  '|';
				break;
			}
		}
		outbuf[x-logoMinX] = 0;
		Debug(1, "%s\n",outbuf);
	}
}

bool CheckFramesForLogo(int start, int end) {
	int		i;
	int		j;
	for (i = start; i <= end; i++) {
		for (j = 0; j < logo_block_count; j++) {
			if (i > logo_block[j].start && i < logo_block[j].end) {
				return (!reverseLogoLogic);
			}
		}
	}

	return (reverseLogoLogic);
}

double CalculateLogoFraction(int start, int end) {
	int		i,j;
	int		count=0;
	bool	logoPresent = false;
	j = 0;
	for (i = start; i <= end; i++) {
		while (j < logo_block_count && i > logo_block[j].end) j++;
		if (j < logo_block_count && i >= logo_block[j].start && i <= logo_block[j].end )
				count++;
	}
	if (reverseLogoLogic)
		return (1.0 - (double) count / (double)(end - start + 1));
	return ((double) count / (double)(end - start + 1));
}

bool CheckFrameForLogo(int i) {
	int		j=0;
	while (j < logo_block_count && i > logo_block[j].end) j++;
	if (j < logo_block_count && i <= logo_block[j].end && i >= logo_block[j].start ){
		return(!reverseLogoLogic);
	}
	return (reverseLogoLogic);
}



char CheckFramesForCommercial(int start, int end) {
	int		i;
	int		j;
	if (start >= end )
		return ('0');						// Too short to decide
	i = 0;
	while (i <= commercial_count && start > commercial[i].end_frame)
		i++;
	if (i <= commercial_count) {			// Now start <= commercial[i].end_frame
		if (end < commercial[i].start_frame)
			return('+');
		if (start < commercial[i].start_frame)
			return('0');
		return('-');
	}
	return('-');
}

char CheckFramesForReffer(int start, int end) {
	int		i;
	int		j;
	if (reffer_count < 0)
		return(' ');
	if (start >= end )
		return ('0');						// Too short to decide
	i = 0;
	while (i <= reffer_count &&  reffer[i].end_frame < start + fps)
		i++;
	if (i <= reffer_count) {			// Now start <= reffer[i].end_frame
		if (reffer[i].start_frame < start + fps)
			return('-');
		if (reffer[i].start_frame > end - fps)
			return('+');
		if ( reffer[i].start_frame < end + fps)
			return('0');
		return('-');
	}
	return('+');
}

void SaveLogoMaskData(void) {
	FILE*	logo_file;
	int		x;
	int		y;
	logo_file = fopen(logofilename, "w");
	if (!logo_file) {
		fprintf(stderr, "%s - could not create file %s\n", strerror(errno), logofilename);
		Debug(1, "%s - could not create file %s\n", strerror(errno), logofilename);
		exit(7);
	}

	fprintf(logo_file, "logoMinX=%i\n", logoMinX);
	fprintf(logo_file, "logoMaxX=%i\n", logoMaxX);
	fprintf(logo_file, "logoMinY=%i\n", logoMinY);
	fprintf(logo_file, "logoMaxY=%i\n", logoMaxY);
	fprintf(logo_file, "picWidth=%i\n", width);
	fprintf(logo_file, "picHeight=%i\n", height);
if (1)
{
	fprintf(logo_file, "\nCombined Logo Mask\n");
	fprintf(logo_file, "\202\n");
	for (y = logoMinY; y <= logoMaxY; y++) {
		for (x = logoMinX; x <= logoMaxX; x++) {
			switch (horiz_edgemask[y * width + x]) {
			case 0:
				if (vert_edgemask[y * width + x] == 1) 
					fprintf(logo_file, "-");
				else
					fprintf(logo_file, " ");
				break;

			case 1:
				if (vert_edgemask[y * width + x] == 1) 
					fprintf(logo_file, "+");
				else
					fprintf(logo_file, "|");
				break;
			}
		}

		fprintf(logo_file, "\n");
	}

}
else
{
	fprintf(logo_file, "\nHorizonatal Logo Mask\n");
	fprintf(logo_file, "\200\n");
	for (y = logoMinY; y <= logoMaxY; y++) {
		for (x = logoMinX; x <= logoMaxX; x++) {
			switch (horiz_edgemask[y * width + x]) {
			case 0:
				fprintf(logo_file, " ");
				break;

			case 1:
				fprintf(logo_file, "|");
				break;
			}
		}

		fprintf(logo_file, "\n");
	}

	fprintf(logo_file, "\nVertical Logo Mask\n");
	fprintf(logo_file, "\201\n");
	for (y = logoMinY; y <= logoMaxY; y++) {
		for (x = logoMinX; x <= logoMaxX; x++) {
			switch (vert_edgemask[y * width + x]) {
			case 0:
				fprintf(logo_file, " ");
				break;

			case 1:
				fprintf(logo_file, "-");
				break;
			}
		}

		fprintf(logo_file, "\n");
	}
}

	fclose(logo_file);
}

void LoadLogoMaskData(void) {
	FILE*	logo_file = NULL;
	FILE*	txt_file;
	int		x;
	int		y;
	double	tmp;
	char	temp;
	char	data[2000];
	char	*ptr = NULL;
	long	tmpLong = 0;
	long	start = 0;
	long	end = 0;
	size_t	len = 0;

	logo_file = fopen(logofilename, "r");
	if (logo_file) {
		Debug(1, "Using %s for logo data.\n", logofilename);
		len = fread(data, 1, 1999, logo_file);
		fclose(logo_file);
		data[len] = '\0';
		if ((tmp = FindNumber(data, "picWidth=", (double) width)) > -1) width = (int)tmp;
		if ((tmp = FindNumber(data, "picHeight=", (double) height)) > -1) height = (int)tmp;
		if ((tmp = FindNumber(data, "logoMinX=", (double) logoMinX)) > -1) logoMinX = (int)tmp;
		if ((tmp = FindNumber(data, "logoMaxX=", (double) logoMaxX)) > -1) logoMaxX = (int)tmp;
		if ((tmp = FindNumber(data, "logoMinY=", (double) logoMinY)) > -1) logoMinY = (int)tmp;
		if ((tmp = FindNumber(data, "logoMaxY=", (double) logoMaxY)) > -1) logoMaxY = (int)tmp;
	} else {
		Debug(0, "Could not find the logo file.\n");
		logoInfoAvailable = false;
		return;
	}

	logo_file = fopen(logofilename, "r");
/*
	horiz_edgemask = malloc(width * height * sizeof(unsigned char));
	if (horiz_edgemask == NULL) {
		Debug(0, "Could not allocate memory for horizontal edgemask\n");
		exit(8);
	}

	vert_edgemask = malloc(width * height * sizeof(unsigned char));
	if (vert_edgemask == NULL) {
		Debug(0, "Could not allocate memory for vertical edgemask\n");
		exit(9);
	}
	memset(horiz_edgemask, 0, width * height);
	memset(vert_edgemask, 0, width * height);
*/
	do
	{
		temp = getc(logo_file);
	} while ((temp != '\200') && !feof(logo_file));
	for (y = logoMinY; y <= logoMaxY; y++) {
		for (x = logoMinX; x <= logoMaxX; x++) {
			temp = getc(logo_file);
			if (temp == '\n') temp = getc(logo_file);				// If a carrage return was retrieved, get the next character
			switch (temp) {
			case ' ':
				horiz_edgemask[y * width + x] = 0;
				break;

			case '|':
				horiz_edgemask[y * width + x] = 1;
				break;
			}
		}
	}

	fclose(logo_file);
	logo_file = fopen(logofilename, "r");
	do
	{
		temp = getc(logo_file);
	} while ((temp != '\201') && !feof(logo_file));
	for (y = logoMinY; y <= logoMaxY; y++) {
		for (x = logoMinX; x <= logoMaxX; x++) {
			temp = getc(logo_file);
			if (temp == '\n') temp = getc(logo_file);				// If a carrage return was retrieved, get the next character
			switch (temp) {
			case ' ':
				vert_edgemask[y * width + x] = 0;
				break;

			case '-':
				vert_edgemask[y * width + x] = 1;
				break;
			}
		}
	}
	fclose(logo_file);

	logo_file = fopen(logofilename, "r");
	do
	{
		temp = getc(logo_file);
	} while ((temp != '\202') && !feof(logo_file));
    if (!feof(logo_file)) {
	for (y = logoMinY; y <= logoMaxY; y++) {
		for (x = logoMinX; x <= logoMaxX; x++) {
			temp = getc(logo_file);
			if (temp == '\n') temp = getc(logo_file);				// If a carrage return was retrieved, get the next character
			switch (temp) {
			case ' ':
				horiz_edgemask[y * width + x] = 0;
				vert_edgemask[y * width + x] = 0;
				break;

			case '-':
				horiz_edgemask[y * width + x] = 0;
				vert_edgemask[y * width + x] = 1;
				break;

			case '|':
				horiz_edgemask[y * width + x] = 1;
				vert_edgemask[y * width + x] = 0;
				break;

			case '+':
				horiz_edgemask[y * width + x] = 1;
				vert_edgemask[y * width + x] = 1;
				break;

			}
		}
	}
    }
	fclose(logo_file);

	
	logoInfoAvailable = true;
	secondLogoSearch = true;
	InitScanLines();
	InitHasLogo();
	isSecondPass = true;
	if (!loadingCSV) {
//		DumpEdgeMask(horiz_edgemask, HORIZ);
//		DumpEdgeMask(vert_edgemask, VERT);
		DumpEdgeMasks();
	}
	memset(data, 0, sizeof(data));
	_flushall();
	if (output_default) {
		txt_file = fopen(out_filename, "r");
		if (!txt_file) {
			Sleep(50L);
			txt_file = fopen(out_filename, "r");
			if (!txt_file) {
				Debug(0, "ERROR reading from %s\n", out_filename);
				isSecondPass = false;
				return;
			}
		}


		if(fseek( txt_file, 0L, SEEK_SET )) {
			Debug(0, "ERROR SEEKING\n");
		}


		while (fgets(data, 1999, txt_file) != NULL) {
			if (strstr(data, "FILE PROCESSING COMPLETE") != NULL) {
				lastFrame = 0;
				break;
			}
			ptr = strchr(data, '\t');
			if (ptr != NULL) {
				ptr++;
				tmpLong = strtol(ptr, NULL, 10);
				if (tmpLong > lastFrame) {
					lastFrame = tmpLong;
				}
			}
		}
		fclose(txt_file);
	}	
	Debug(10, "The last frame found in %s was %i\n", out_filename, lastFrame);
}

int CountSceneChanges(int StartFrame, int EndFrame) {
	int i;
	double p = 0;
	int count = 0;
	for (i = 0; i < schange_count; i++) {
		if ((schange[i].frame > StartFrame) && (schange[i].frame < EndFrame)) {
			count++;
			p += (double)(100 - schange[i].percentage)  / (100 - schange_threshold);
		}
	}
	count = (int) p;

	return (count);
}

void Debug(int level, char* fmt, ...)
{
	va_list	ap;
	FILE *log_file = NULL;
	if(verbose < level) return;
	
	va_start(ap, fmt);
	vsprintf(debugText, fmt, ap);
	va_end(ap);

	if (output_console)	_cprintf("%s", debugText);

	if (!log_file)
		log_file = fopen(logfilename, "a+");

	if (log_file) {
		fprintf(log_file, "%s", debugText);
		fclose(log_file);
		log_file = NULL;
	}


	debugText[0] = '\0';
}

void InitLogoBuffers(void) {
	int i;
	if(!logoFrameNum) logoFrameNum = malloc(num_logo_buffers * sizeof(int));
	if (logoFrameNum == NULL) {
		Debug(0, "Could not allocate memory for logo buffer frame number array\n");
		exit(14);
	}
	memset(logoFrameNum, 0,num_logo_buffers*sizeof(int));
/*
	if(!horiz_edgemask) horiz_edgemask = malloc(width * height * sizeof(unsigned char));
	if (horiz_edgemask == NULL) {
		Debug(0, "Could not allocate memory for horizontal edgemask\n");
		exit(14);
	}
	
	if(!vert_edgemask) vert_edgemask = malloc(width * height * sizeof(unsigned char));
	if (vert_edgemask == NULL) {
		Debug(0, "Could not allocate memory for vertical edgemask\n");
		exit(15);
	}
*/	
	if(!logoFrameBuffer) logoFrameBuffer = malloc(num_logo_buffers * sizeof(unsigned char *));
	if (!(logoFrameBuffer == NULL)) {

		logoFrameBufferSize = width * height * sizeof(frame_ptr[0]);
		lheight = height;
		lwidth = width;
		for (i = 0; i < num_logo_buffers; i++) {
			logoFrameBuffer[i] = malloc(logoFrameBufferSize);
			if (logoFrameBuffer[i] == NULL) {
				Debug(0, "Could not allocate memory for logo frame buffer %i\n", i);
				exit(16);
			}
		}
	} else {
		Debug(0, "Could not allocate memory for logo frame buffers\n");
		exit(16);
	}
#if MULTI_EDGE_BUFFER
	if(!horiz_edges) horiz_edges = malloc(num_logo_buffers * sizeof(unsigned char *));
	if (!(horiz_edges == NULL)) {
		for (i = 0; i < num_logo_buffers; i++) {
			horiz_edges[i] = malloc(width * height * sizeof(unsigned char));
			if (horiz_edges[i] == NULL) {
				Debug(0, "Could not allocate memory for horizontal edge buffer %i\n", i);
				exit(17);
			}
		}
	} else {
		Debug(0, "Could not allocate memory for horizontal edge buffers\n");
		exit(18);
	}
#else
/*
	horiz_count = malloc(width * height * sizeof(unsigned char));
	if (horiz_count == NULL) {
		Debug(0, "Could not allocate memory for horizontal count buffer\n");
		exit(17);
	}
	memset(horiz_count, 0, width * height * sizeof(unsigned char));
*/
#endif
	
#if MULTI_EDGE_BUFFER
	if(!vert_edges) vert_edges = malloc(num_logo_buffers * sizeof(unsigned char *));
	if (!(vert_edges == NULL)) {
		for (i = 0; i < num_logo_buffers; i++) {
			vert_edges[i] = malloc(width * height * sizeof(unsigned char));
			if (vert_edges[i] == NULL) {
				Debug(0, "Could not allocate memory for vertical edge buffer %i\n", i);
				exit(19);
			}
		}
	} else {
		Debug(0, "Could not allocate memory for vertical edge buffers\n");
		exit(20);
	}
#else
/*
	vert_count = malloc(width * height * sizeof(unsigned char));
	if (vert_count == NULL) {
		Debug(0, "Could not allocate memory for vertical count buffer\n");
		exit(17);
	}
	memset(vert_count, 0, width * height * sizeof(unsigned char));
*/
#endif
}

void InitComSkip(void) {
	int i, j;
	min_brightness_found = 255;
	max_logo_gap = -1;
	max_nonlogo_block_length = -1;
	logo_overshoot = 0;
	for (i = 0; i < 256; i++) brightHistogram[i] = 0;
	for (i = 0; i < 256; i++) uniformHistogram[i] = 0;
	for (i = 0; i < 256; i++) volumeHistogram[i] = 0;
	if (framearray) {
		if(!initialized) {
			max_frame_count = (int)(60 * 60 * fps) + 1;
			frame = malloc((int)((max_frame_count + 1) * sizeof(frame_info)));
		}
		if (frame == NULL) {
			Debug(0, "Could not allocate memory for frame array\n");
			exit(10);
		}
	}

//	if (commDetectMethod & BLACK_FRAME) {
		if(!initialized) {
			max_black_count = 500;
			black = malloc((int)((max_black_count + 1) * sizeof(black_frame_info)));
		}
		if (black == NULL) {
			Debug(0, "Could not allocate memory for black frame array\n");
			exit(11);
		}
//	} else {
//		Debug(1, "ERROR: ComSkip cannot run without black frames.\n");
//		exit(100);
//	}

	if (commDetectMethod & LOGO) {
		if(!initialized) {
			max_logo_block_count = 1;
			logo_block = malloc((int)((max_logo_block_count + 1) * sizeof(logo_block_info)));
		}
		if (logo_block == NULL) {
			Debug(0, "Could not allocate memory for logo cblock array\n");
			exit(13);
		}

//		if (!logoInfoAvailable) {
			InitLogoBuffers();
//		}
		memset(max_br,   0, sizeof(max_br));
		memset(min_br, 255, sizeof(min_br));

	}

	if (commDetectMethod & SCENE_CHANGE) {
		if(!initialized) {
			max_schange_count = 2000;
			schange = malloc((int)((max_schange_count + 1) * sizeof(schange_info)));
		}
		if (schange == NULL) {
			Debug(0, "Could not allocate memory for scene change array\n");
			exit(12);
		}
	}

	if (processCC) {
		if(!initialized) {
			max_cc_block_count = 500;
			cc_block = malloc((max_cc_block_count + 1) * sizeof(cc_block_info));
		}
		if (cc_block == NULL) {
			Debug(0, "Could not allocate memory for cc blocks\n");
			exit(22);
		}

		cc_block[0].start_frame = 0;
		cc_block[0].end_frame = -1;
		cc_block[0].type = NONE;
		for (i = 1; i < max_cc_block_count; i++) {
			cc_block[i].start_frame = -1;
			cc_block[i].end_frame = -1;
			cc_block[i].type = NONE;
		}

		if(!initialized) {
			cc_memory = malloc(15 * sizeof(unsigned char *));
			cc_screen = malloc(15 * sizeof(unsigned char *));
			for (i = 0; i < 15; i++) {
				cc_memory[i] = malloc(32 * sizeof(unsigned char));
				cc_screen[i] = malloc(32 * sizeof(unsigned char));
			}
		}
		for(i=0; i<15; i++){
			for (j = 0; j < 32; j++) {
				cc_memory[i][j] = 0;
				cc_screen[i][j] = 0;
			}
		}

		if(!initialized) {
			max_cc_text_count = 1;
			cc_text = malloc((max_cc_text_count + 1) * sizeof(cc_text_info));
		}
		if (cc_text == NULL) {
			Debug(0, "Could not allocate memory for cc text groups\n");
			exit(22);
		}

		cc_text[0].start_frame = 1;
		cc_text[0].end_frame = -1;
		cc_text[0].text[0] = '\0';
		cc_text[0].text_len = 0;
		for (i = 1; i < max_cc_text_count; i++) {
			cc_text[i].start_frame = -1;
			cc_text[i].end_frame = -1;
			cc_text[i].text[0] = '\0';
			cc_text[i].text_len = 0;
		}
	}

//	if (commDetectMethod & AR) {
		if(!initialized) {
			max_ar_block_count = 100;
			ar_block = malloc((int)((max_ar_block_count + 1) * sizeof(ar_block_info)));
		}
		if (ar_block == NULL) {
			Debug(0, "Could not allocate memory for aspect ratio cblock array\n");
			exit(31);
		}
//	}

	cc.cc1[0] = 0;
	cc.cc1[1] = 0;
	cc.cc2[0] = 0;
	cc.cc2[1] = 0;
	lastcc.cc1[0] = 0;
	lastcc.cc1[1] = 0;
	lastcc.cc2[0] = 0;
	lastcc.cc2[1] = 0;

	Init_XDS_block();

	if (max_avg_brightness == 0) {
		if (fps == 25.00) 
			max_avg_brightness = 19;
		else
			max_avg_brightness = 19;
	}
	schange_count = 0;
	frame_count	= 0;
	framesprocessed =0;
	black_count = 0;
	block_count = 0;
	ar_block_count = 0;
	framenum_real = 0;
	frames_with_logo = 0;
	framenum = 0;
	logoTrendCounter = 0;
//	audio_framenum = 0;
	cc_block_count = 0;
	cc_text_count = 0;
	logo_block_count = 0;
	pts = dts = apts = adts = 0;
	ascr=scr=0;
	InitScanLines();
	InitHasLogo();
	initialized = true;
	close_dump();
}

void FindIniFile(void) {
#ifdef _WIN32
	char	searchinifile[] = "comskip.ini";
	char	searchexefile[] = "comskip.exe";
	char	searchdictfile[] = "comskip.dictionary";
	char	envvar[] = "PATH";
	_searchenv(searchinifile, envvar, inifilename);
	if (*inifilename != '\0') {
		Debug(1, "Path for %s: %s\n", searchinifile, inifilename);
	} else {
		Debug(1, "%s not found\n", searchinifile);
	}

	_searchenv(searchdictfile, envvar, dictfilename);
	if (*dictfilename != '\0') {
		Debug(1, "Path for %s: %s\n", searchdictfile, dictfilename);
	} else {
		Debug(1, "%s not found\n", searchdictfile);
	}

	_searchenv(searchexefile, envvar, exefilename);
	if (*exefilename != '\0') {
		Debug(1, "Path for %s: %s\n", searchexefile, exefilename);
	} else {
		Debug(1, "%s not found\n", searchexefile);
	}
#endif
}

double FindScoreThreshold(double percentile) {
	int			i;
	int			counter;
	double*		score = NULL;
	long*		count = NULL;
	long*		start = NULL;
	double*		percent = NULL;
	int*		blocknr = NULL;
	double		tempScore;
	long		tempCount;
	long		tempStart;
	int			tempBlocknr;
	long		targetCount;
	long		totalframes = 0;
	bool		hadToSwap = false;
	score = malloc(sizeof(cblock[0].score) * block_count);
	count = malloc(sizeof(long) * block_count);
	start = malloc(sizeof(long) * block_count);
	blocknr = malloc(sizeof(int) * block_count);
	percent = malloc(sizeof(double) * block_count);
	if ((score == NULL) || (count == NULL) || (start == NULL) || (blocknr == NULL) || (percent == NULL)) {
		Debug(1, "Could not allocate memory.  Exiting program.\n");
		exit(21);
	}

	counter = 0;
	for (i = 0; i < block_count; i++) {
		blocknr[i] = i;
		score[i] = cblock[i].score;
		count[i] = cblock[i].f_end - cblock[i].f_start + 1;
		start[i] = cblock[i].f_start;
	}

	do
	{
		hadToSwap = false;
		counter++;
		for (i = 0; i < block_count - 1; i++) {
			if (score[i] > score[i + 1]) {
				hadToSwap = true;
				tempScore = score[i];
				tempCount = count[i];
				tempStart = start[i];
				tempBlocknr = blocknr[i];
				score[i] = score[i + 1];
				count[i] = count[i + 1];
				start[i] = start[i + 1];
				blocknr[i] = blocknr[i + 1];
				score[i + 1] = tempScore;
				count[i + 1] = tempCount;
				start[i + 1] = tempStart;
				blocknr[i + 1] = tempBlocknr;
			}
		}
	} while (hadToSwap);
	for (i = 0; i < block_count; i++) {
		totalframes += count[i];
	}

	tempCount = 0;
	Debug(10, "\n\nAfter Sorting - %i\n--------------\n", counter);
	for (i = 0; i < block_count; i++) {
		tempCount += count[i];
		Debug(10, "Block %3i - %.3f\t%6i\t%6i\t%6i\t%3.1f%c\n", blocknr[i], score[i], start[i], cblock[blocknr[i]].f_end, count[i], ((double)tempCount / (double)totalframes)*100,'%');
	}

	targetCount = (long)(totalframes * percentile);
	i = -1;
	tempCount = 0;
	do
	{
		i++;
		tempCount += count[i];
	} while (tempCount < targetCount);
	tempScore = score[i];
	free(score);
	free(count);
	free(percent);
	Debug(6, "The %.2f percentile of %i frames is %.2f\n", (percentile * 100.0), totalframes, tempScore);
	return (tempScore);
}

void OutputLogoHistogram(int buckets) {
	int		i;
	int		j;
	long	max = 0;
	int		columns = 200;
	double	divisor;
	char stars[256];
	long	counter = 0;
	
	for (i = 0; i < buckets; i++) {
		if (max < logoHistogram[i]) {
			max = logoHistogram[i];
		}
	}

	divisor = (double)columns / (double)max;
	
	Debug(8, "Logo Histogram - %.5f\n", divisor);
	
	for (i = 0; i < buckets; i++) {
		counter += logoHistogram[i];
		stars[0] = 0;
		if (logoHistogram[i] > 0) {
			for (j = 0; j <= (int)(logoHistogram[i] * divisor); j++) {
				stars[j] = '*';
			}
			stars[j] = 0;
		}
		Debug(8, "%.3f - %6i - %.5f %s\n", (double)i/buckets, logoHistogram[i], (double)counter / (double)frame_count, stars);
	}
}



void OutputbrightHistogram(void) {
	int		i;
	int		j;
	long	max = 0;
	int		columns = 200;
	double	divisor;
	long	counter = 0;
	char stars[256];
	
	for (i = 0; i < 256; i++) {
		if (max < brightHistogram[i]) {
			max = brightHistogram[i];
		}
	}

	divisor = (double)columns / (double)max;
	
	Debug(1, "Show Histogram - %.5f\n", divisor);
	
	for (i = 0; i < 30; i++) {
		counter += brightHistogram[i];
		stars[0] = 0;
		if (brightHistogram[i] > 0) {
			for (j = 0; j <= (int)(brightHistogram[i] * divisor); j++) {
				stars[j] = '*';
			}
			stars[j] = 0;
		}
		Debug(1, "%3i - %6i - %.5f %s\n", i, brightHistogram[i], (double)counter / (double)framesprocessed, stars);
	}
}

void OutputuniformHistogram(void) {
	int		i;
	int		j;
	long	max = 0;
	int		columns = 200;
	double	divisor;
	long	counter = 0;
	char stars[256];
	
	for (i = 0; i < 30; i++) {
		if (max < uniformHistogram[i]) {
			max = uniformHistogram[i];
		}
	}

	divisor = (double)columns / (double)max;
	
	Debug(1, "Show Uniform - %.5f\n", divisor);
	
	for (i = 0; i < 30; i++) {
		counter += uniformHistogram[i];
		stars[0] = 0;
		if (uniformHistogram[i] > 0) {
			for (j = 0; j <= (int)(uniformHistogram[i] * divisor); j++) {
				stars[j] = '*';
			}
			stars[j] = 0;
		}
		Debug(1, "%3i - %6i - %.5f %s\n", i*UNIFORMSCALE, uniformHistogram[i], (double)counter / (double)framesprocessed,stars);
	}
}

void OutputHistogram(int *histogram, int scale, char *title, bool truncate) {
	int		i;
	int		j;
	long	max = 0;
	int		columns = 70;
	double	divisor;
	long	counter = 0;
	char stars[256];
	
	for (i = 0; i < (truncate?255:256); i++) {
		if (max < histogram[i]) {
			max = histogram[i];
		}
	}

	divisor = (double)columns / (double)max;
	
	Debug(8, "Show %s Histogram\n", title);
	
	for (i = 0; i < 256; i++) {
		counter += histogram[i];
		stars[0] = 0;
		if (histogram[i] > 0) {
			for (j = 0; j <= (int)(histogram[i] * divisor) && j <= columns; j++) {
				stars[j] = '*';
			}
			stars[j] = 0;
		}
		Debug(8, "%3i - %6i - %.5f %s\n", i*scale, histogram[i], (double)counter / (double)framesprocessed, stars);
	}
}


int FindBlackThreshold(double percentile) {
	int		i;
	long	tempCount;
	long	targetCount;
	long	totalframes = 0;

	FILE *raw = NULL;
	if (output_training) raw = fopen("black.csv", "a+");

	if (raw) fprintf(raw, "\"%s\"", basename);
	for (i = 0; i < 256; i++) {
		totalframes += brightHistogram[i];
	}
	
	for (i = 0; i < 35; i++) {
		if (raw) fprintf(raw, ",%6.2f", (1000.0*(double)brightHistogram[i])/totalframes);
	}
	if (raw) fprintf(raw, "\n");
	if (raw) fclose(raw);

	tempCount = 0;
	targetCount = (long)(totalframes * percentile);
	i = -1;
	tempCount = 0;
	do
	{
		i++;
		tempCount += brightHistogram[i];
	} while (tempCount < targetCount);
	return (i);
}

int FindUniformThreshold(double percentile) {
	int		i;
	long	tempCount;
	long	targetCount;
	long	totalframes = 0;

	FILE *raw = NULL;
	
	if (output_training) raw = fopen("uniform.csv", "a+");
	if (raw) fprintf(raw, "\"%s\"", basename);

	for (i = 0; i < 256; i++) {
		totalframes += uniformHistogram[i];
	}
	for (i = 0; i < 35; i++) {
		if (raw) fprintf(raw, ",%6.2f", (1000.0*(double)uniformHistogram[i])/totalframes);
	}
	if (raw) fprintf(raw, "\n");
	if (raw) fclose(raw);

	tempCount = 0;
	targetCount = (long)(totalframes * percentile);
	i = -1;
	tempCount = 0;
	do
	{
		i++;
		tempCount += uniformHistogram[i];
	} while (tempCount < targetCount);
	if (i == 0)
		i = 1;
//	while (uniformHistogram[i+1] < uniformHistogram[i])
//		i++;
	return ((i+1)*UNIFORMSCALE);
}

void OutputFrame(int frame_number) {
	int		x,y;
	FILE*	raw;
	char	array[MAX_PATH];
  sprintf(array, "%.*s%i.frm", (int)(strlen(logfilename) - 4), logfilename,frame_number);

	Debug(5, "Sending frame to file\n");
	raw = fopen(array, "w");
	if (!raw) {
		Debug(1, "Could not open frame output file.\n");
		return;
	}

	fprintf(raw, "0;");
	for (x = 0; x < width; x++) {
		fprintf(raw, ";%3i", x);
	}
	fprintf(raw, "\n");

	for (y = 0; y < height; y++) {
		fprintf(raw, "%3i", y);
		for (x = 0; x < width; x++) {
			if (frame_ptr[y * width + x] < 30)
				fprintf(raw, ";   ", frame_ptr[y * width + x]);
			else
				fprintf(raw, ";%3i", frame_ptr[y * width + x]);

		}
		fprintf(raw, "\n");
	}
	fclose(raw);
}


int InputReffer(char *extension) {
	int		i;
	long	j;
	int		k, pk;
	double	t;
	enum {both_show,both_commercial, only_reffer, only_commercial} state;
	char	line[2048];
	char	split[256];
	char	array[MAX_PATH];
	FILE*	raw;
	int		x;
	int		col;
	bool	lineProcessed;
	int     frames;
	char	co,re;
	FILE*    raw2=NULL;
	sprintf(array, "%.*s%s", (int)(strlen(logfilename) - 4), logfilename,extension);
	raw = fopen(array, "r");
	if (!raw) {
		return(0);
	}

	fgets(line, sizeof(line), raw); // Read first line

	frames = 0;
	if (strlen(line) > 27)
		frames = strtol(&line[25], NULL, 10);

	if (strlen(line) > 42)
		t = ((double)strtol(&line[42], NULL, 10))/100;
	if (t == 25 || t == 29.97 || t == 59.94)
		fps = t;

	if (t != 59.94)
		sage_framenumber_bug = false;

	reffer_count = -1;
	fgets(line, sizeof(line), raw); // Skip second line
	while (fgets(line, sizeof(line), raw) != NULL && strlen(line) > 1) {
		if (line[strlen(line)-1] != '\n') {
			strcat(&line[strlen(line)], "\n");
		}
		i = 0;
		x = 0;
		col = 0;
		lineProcessed = false;
		reffer_count++;
		// Split Line Apart
    while (line[i] != '\0' && i < (int)sizeof(line) && !lineProcessed) {
			if (line[i] == ' ' || line[i] == '\t' || line[i] == '\n') {
				split[x] = '\0';

				switch (col) {
				case 0:
					reffer[reffer_count].start_frame = strtol(split, NULL, 10);
					if (sage_framenumber_bug) reffer[reffer_count].start_frame *= 2;
					break;

				case 1:
					reffer[reffer_count].end_frame = strtol(split, NULL, 10);
					if (reffer[reffer_count].end_frame < reffer[reffer_count].start_frame){
						Debug(0,"Error in .ref file, end < start frame\n");
						reffer[reffer_count].end_frame = reffer[reffer_count].start_frame + 10;
					}
					if (sage_framenumber_bug) reffer[reffer_count].end_frame *= 2;
					lineProcessed = true;
					break;
				}
				col++;
				x = 0;
				split[0] = '\0';
			} else {
				split[x] = line[i];
				x++;
			}
			i++;
		}
	}
	fclose(raw);
	if (reffer_count >= 0) {
		if (frames == 0)
			frames = reffer[reffer_count].end_frame;
		if (reffer[reffer_count].end_frame == reffer[reffer_count].start_frame+1 &&
			reffer[reffer_count].end_frame == frames)
			reffer_count--;
	}

	if (extension[1] == 't')
		return(frames);

  sprintf(array, "%.*s.dif", (int)(strlen(logfilename) - 4), logfilename);
	raw = fopen(array, "w");
	if (!raw) {
		return(0);
	}


//#ifdef faslpositive_negative
	j = 0;
	i = 0;
	state = both_show;
	k = 0;
	while ( k < commercial[commercial_count].end_frame && 
			(i <= reffer_count || j <= commercial_count) ) {
		pk = k;
		switch(state) {
		case both_show:
			if (abs(reffer[i].start_frame-commercial[j].start_frame) < 40) {
				state = both_commercial;
				k = commercial[j].start_frame;
			} else if (commercial[j].start_frame < reffer[i].start_frame) {
				state = only_commercial;
				k = commercial[j].start_frame;
			} else {
				state = only_reffer;
				k = reffer[i].start_frame;
			}
			break;
		case both_commercial:
			if (abs(reffer[i].end_frame-commercial[j].end_frame) < 40) {
				state = both_show;
				k = commercial[j].end_frame;
				if (i <= reffer_count) i++; 
				if (j <= commercial_count) j++;
			} else if (commercial[j].end_frame < reffer[i].end_frame) {
				state = only_reffer;
				k = commercial[j].end_frame;
				if (j <= commercial_count) j++;
			} else {
				state = only_commercial;
				k = reffer[i].end_frame;
				if (i <= reffer_count) i++; 
			}
			break;
		case only_reffer:
			if (j > commercial_count || reffer[i].end_frame < commercial[j].start_frame) {
				state = both_show;
				if (i <= reffer_count)
					k = reffer[i].end_frame;
				else
					k = commercial[commercial_count].end_frame;
				if (i <= reffer_count) i++; 
			} else {
				state = both_commercial;
				k = commercial[j].start_frame;
			}
//			fprintf(raw, "False negative at frame %6ld of %6.1f seconds\n", pk , (k - pk)/fps );
			if (output_training) raw2 = fopen("quality.csv", "a+");
			if (raw2) fprintf(raw2, "\"%s\", %6ld, %6.1f, %6.1f\n", basename, pk, (k - pk)/fps, 0.0);
			if (raw2) fclose(raw2);
			raw2 = NULL;
			break;
		case only_commercial:
			if (i > reffer_count || commercial[j].end_frame < reffer[i].start_frame) {
				state = both_show;
				k = commercial[j].end_frame;
				if (j <= commercial_count) j++;
			} else {
				state = both_commercial;
				k = reffer[i].start_frame;
			}
//			fprintf(raw, "False positive at frame %6ld of %6.1f seconds\n", pk , (k - pk)/fps );
			if (output_training) raw2 = fopen("quality.csv", "a+");
			if (raw2) fprintf(raw2, "\"%s\", %6ld, %6.1f, %6.1f\n", basename, pk, 0.0, (k - pk)/fps);
			if (raw2) fclose(raw2);
			raw2 = NULL;
			break;
		}
	}

//#else
	j = 0;
	i = 0;
	while ( i <= reffer_count && j <= commercial_count ) {
		k = min(reffer[i].start_frame, commercial[j].start_frame);
		if ( commercial[j].end_frame < reffer[i].start_frame ) {
			fprintf(raw, "Found %6ld %6ld    Reference %6ld %6ld    Difference %+6.1f    %+6.1f\n", commercial[j].start_frame, commercial[j].end_frame, 0,0, (commercial[j].end_frame - commercial[j].start_frame)/fps , (commercial[j].end_frame - commercial[j].start_frame)/fps);
//			fprintf(raw, "Found %6ld %6ld    Not in reference\n", commercial[j].start_frame, commercial[j].end_frame);
			j++;
		} else if ( commercial[j].start_frame > reffer[i].end_frame ) {
			fprintf(raw, "Found %6ld %6ld    Reference %6ld %6ld    Difference %+6.1f    %+6.1f\n", 0, 0, reffer[i].start_frame, reffer[i].end_frame, -(reffer[i].end_frame - reffer[i].start_frame)/fps , -(reffer[i].end_frame - reffer[i].start_frame)/fps);
//			fprintf(raw, "Not found %6ld %6ld\n", reffer[i].start_frame, reffer[i].end_frame);
			i++;
		} else { 	
			if (abs(reffer[i].start_frame-commercial[j].start_frame) > 40 || 
				abs(reffer[i].end_frame-commercial[j].end_frame) > 40 ) {
				fprintf(raw, "Found %6ld %6ld    Reference %6ld %6ld    Difference %+6.1f    %+6.1f\n", commercial[j].start_frame, commercial[j].end_frame, reffer[i].start_frame, reffer[i].end_frame, (reffer[i].start_frame - commercial[j].start_frame)/fps , (commercial[j].end_frame - reffer[i].end_frame)/fps);
			}
/*
			if (abs(reffer[i].start_frame-commercial[j].start_frame) > 40 ) {
				fprintf(raw, "Found %5ld %5ld    Reference %5ld %5ld    ", commercial[j].start_frame, commercial[j].end_frame, reffer[i].start_frame, reffer[i].end_frame);
				fprintf(raw, "starts at %5ld instead of %5ld\n", commercial[j].start_frame, reffer[i].start_frame);
			}
			if (abs(reffer[i].end_frame-commercial[j].end_frame) > 40 ) {
				fprintf(raw, "Found %5ld %5ld    Reference %5ld %5ld    ", commercial[j].start_frame, commercial[j].end_frame, reffer[i].start_frame, reffer[i].end_frame);
				fprintf(raw, "ends   at %5ld instead of %5ld\n", commercial[j].end_frame, reffer[i].end_frame);
			}
*/
			i++;
			j++;
		}
	}
	while (j <= commercial_count) {
		fprintf(raw, "Found %6ld %6ld    Reference %6ld %6ld    Difference %+6.1f    %+6.1f\n", commercial[j].start_frame, commercial[j].end_frame, 0,0, (commercial[j].end_frame - commercial[j].start_frame)/fps , (commercial[j].end_frame - commercial[j].start_frame)/fps);
//		fprintf(raw, "Found %6ld %6ld    Not in reference\n", commercial[j].start_frame, commercial[j].end_frame);
		j++;
	}
	while (i <= reffer_count) {
		fprintf(raw, "Found %6ld %6ld    Reference %6ld %6ld    Difference %+6.1f    %+6.1f\n", 0, 0, reffer[i].start_frame, reffer[i].end_frame, -(reffer[i].end_frame - reffer[i].start_frame)/fps , -(reffer[i].end_frame - reffer[i].start_frame)/fps);
//		fprintf(raw, "Not found %6ld %6ld\n", reffer[i].start_frame, reffer[i].end_frame);
		i++;
	}
//#endif
	for (i=0; i<block_count; i++) {
		co = CheckFramesForCommercial(cblock[i].f_start+cblock[i].b_head,cblock[i].f_end - cblock[i].b_tail);
		re = CheckFramesForReffer(cblock[i].f_start+cblock[i].b_head,cblock[i].f_end - cblock[i].b_tail);
		if (co != re) {
			fprintf(raw, "Block %6d has mismatch %c%c with cause %s\n", i,co,re, CauseString(cblock[i].cause));
		}
		cblock[i].reffer = re;
	}

	fclose(raw);
	return(frames);
}


void OutputAspect(void) {
	int		i;
//	long	j;
	char	array[MAX_PATH];
	FILE*	raw;
	char	lp[10];

	if (!output_aspect)
		return;
	
	sprintf(array, "%.*s.aspects", strlen(logfilename) - 4, logfilename);
	raw = fopen(array, "w");
	if (!raw) {
		Debug(1, "Could not open aspect output file.\n");
		return;
	}

		// Print out ar cblock list
		for (i = 0; i < ar_block_count; i++) {
			fprintf(
				raw,
				"%s %4dx%4d %.2f minX=%4d, minY=%4d, maxX=%4d, maxY=%4d\n",
				dblSecondsToStrMinutes((ar_block[i].start) / fps),
				ar_block[i].width, ar_block[i].height,
				ar_block[i].ar_ratio,
				ar_block[i].minX, ar_block[i].minY, ar_block[i].maxX, ar_block[i].maxY
			);
		}
	fclose(raw);
}




void OutputFrameArray(bool screenOnly) {
	int		i;
#ifdef FRAME_WITH_HISTOGRAM
	int		k;
#endif
//	long	j;
	char	array[MAX_PATH];
	FILE*	raw;
	char	lp[10];
  sprintf(array, "%.*s.csv", (int)(strlen(logfilename) - 4), logfilename);
//	Debug(5, "Expanding logo blocks into frame array\n");
//	for (i = 0; i < logo_block_count; i++) {
//		for (j = logo_block[i].start; j <= logo_block[i].end; j++) {
//			frame[j].logo_present = TRUE;
//		}
//	}
//	Debug(5, "Expanded logo blocks into frame array\n");
	raw = fopen(array, "w");
	if (!raw) {
		Debug(1, "Could not open raw output file.\n");
		return;
	}
	fprintf(raw, "frame,brightness,scene_change,logo,uniform,sound,minY,MaxY,ar_ratio,goodEdge,isblack,cutscene,%d",(int)(fps*100));
//	for (k = 0; k < 32; k++) {
//		fprintf(raw, ",b%3i", k);
//	}
	fprintf(raw, "\n");



	if (screenOnly)
		Debug(1, "Frame\tBlack\tBrightness\tS_Change\tS_Change Perc\tLogo Present\t%i\n", frame_count);
	for (i = 1; i < frame_count; i++) {
		if (screenOnly) {
			printf("%i\t%i\t%i\t%s\tHistogram\n", i, frame[i].brightness, frame[i].schange_percent, lp);
		} else {
			fprintf(raw, "%i,%i,%i,%i,%i,%i,%i,%i,%i,%i,%i,%i,%i,%i,%i,%i", 
				i, frame[i].brightness, frame[i].schange_percent*5, frame[i].logo_present, 
				frame[i].uniform, frame[i].volume,  frame[i].minY,frame[i].maxY,(int)((frame[i].ar_ratio)*100), 
				(int)(frame[i].currentGoodEdge * 500), frame[i].isblack,(int)frame[i].cutscenematch,  frame[i].minX,frame[i].maxX,frame[i].hasBright,frame[i].dimCount
				);
#ifdef FRAME_WITH_HISTOGRAM
			for (k = 0; k < 32; k++) {
				fprintf(raw, ",%i", frame[i].histogram[k]);
			}
#endif
			fprintf(raw, "\n");
		}
	}

	fclose(raw);
}

void InitializeFrameArray(long i) {
	if (frame_count+1000 /* max size audio can run ahead of video */ >= max_frame_count) {
		max_frame_count += (int)(15 * 60 * 25);
		frame = realloc(frame, max_frame_count * sizeof(frame_info));
		Debug(9, "Resizing frame array to accomodate %i frames.\n", max_frame_count);
	}

	frame[i].brightness = 0;
//	frame[i].frame = i;
	frame[i].logo_present = false;
	frame[i].schange_percent = 100;
	frame[i].cutscenematch = 0;
#ifdef FRAME_WITH_HISTOGRAM
	memset(frame[i].histogram, 0, sizeof(frame[i].histogram));
#endif
	frame[i].uniform = 0;
	frame[i].ar_ratio = AR_UNDEF;
	frame[i].minY = 0;
	frame[i].maxY = 0;
	frame[i].isblack = 0;
	if (i > 0)
		frame[i].xds = frame[i-1].xds;
	else
		frame[i].xds = 0;
//	frame[i].volume = curvolume;

}

void InitializeBlackArray(long i) {
	if (black_count >= max_black_count) {
		max_black_count += 500;
		black = realloc(black, (max_black_count + 1) * sizeof(black_frame_info));
		Debug(9, "Resizing black frame array to accomodate %i frames.\n", max_black_count);
	}

	black[i].brightness = 255;
	black[i].uniform = 0;
	black[i].volume = curvolume;
	//	black[i].frame = i; Wrong!!!!!!!!!!!!!!!!!!!!!!!!!
}

void InitializeSchangeArray(long i) {
	if (schange_count >= max_schange_count) {
		max_schange_count += 2000;
		schange = realloc(schange, (max_schange_count + 1) * sizeof(schange_info));
		Debug(9, "Resizing scene change array to accomodate %i frames.\n", max_schange_count);
	}

	schange[i].frame = i;
	schange[i].percentage = 100;
}

void InitializeLogoBlockArray(long i) {
	if (logo_block_count >= max_logo_block_count) {
		max_logo_block_count += 20;
		logo_block = realloc(logo_block, (max_logo_block_count + 2) * sizeof(logo_block_info));
		Debug(9, "Resizing logo cblock array to accomodate %i logo groups.\n", max_logo_block_count);
	}
}

void InitializeARBlockArray(long i) {
	if (ar_block_count >= max_ar_block_count) {
		max_ar_block_count += 20;
		ar_block = realloc(ar_block, (max_ar_block_count + 2) * sizeof(ar_block_info));
		Debug(9, "Resizing aspect ratio cblock array to accomodate %i AR groups.\n", max_ar_block_count);
	}
}

void InitializeBlockArray(long i) {
	if (block_count >= max_block_count) {
		Debug(0,"Panic, too many blocks\n");
		exit(102);
//		max_block_count += 100;
//		cblock = realloc(cblock, (max_block_count + 1) * sizeof(block_info));
//		Debug(9, "Resizing cblock array to accomodate %i blocks.\n", max_block_count);
	}

	cblock[i].f_start = 0;
	cblock[i].f_end = 0;
	cblock[i].b_head = 0;
	cblock[i].b_tail = 0;
	cblock[i].bframe_count = 0;
	cblock[i].schange_count = 0;
	cblock[i].schange_rate = 0;
	cblock[i].length = 0;
	cblock[i].score = 1.0;
	cblock[i].combined_count = 0;
	cblock[i].ar_ratio = AR_UNDEF;
	cblock[i].brightness = 0;
	cblock[i].volume = 0;
	cblock[i].silence = 0;
	cblock[i].stdev = 0;
	cblock[i].cause = 0;
	cblock[i].less = 0;
	cblock[i].more = 0;
	cblock[i].uniform = 0;
}

void InitializeCCBlockArray(long i) {
	if (cc_block_count >= max_cc_block_count) {
		max_cc_block_count += 100;
		cc_block = realloc(cc_block, (max_cc_block_count + 2) * sizeof(cc_block_info));
		Debug(9, "Resizing cc cblock array to accomodate %i cc blocks.\n", max_cc_block_count);
	}

	cc_block[i].start_frame = -1;
	cc_block[i].end_frame = -1;
	cc_block[i].type = NONE;
}

void InitializeCCTextArray(long i) {
	if (cc_text_count >= max_cc_text_count) {
		max_cc_text_count += 100;
		cc_text = realloc(cc_text, (max_cc_text_count + 1) * sizeof(cc_text_info));
		Debug(9, "Resizing cc text array to accomodate %i cc text groups.\n", max_cc_text_count);
	}

	cc_text[i].text[0] = '\0';
	cc_text[i].text_len = 0;
}

void PrintArgs(void) {
	int i;
	for (i = 0; i < argument_count; i++) printf("%i\t%s\n", i, argument[i]);
}




void ProcessCSV(FILE *in_file) {
	bool	lineProcessed = false;
	bool	lastFrameHadLogo = false;
	int		logoTrendCounter = 0;
	bool	lastLogoTest = false,curLogoTest = false;
//	bool	isDim = false;
	char	line[2048];
	char	split[256];
	int		cont;

	int		old_height = 0;
	int		old_width = 0;
	int		minminY=10000,maxmaxY = 0;
	int		minminX=10000,maxmaxX = 0;
	int		hi;
	int		silence_count = 0;
	int     schange_found = 0;
	int		cutscene_nonzero_count = 0;
	int old_format = true;
	int     use_bright = 0;
	double  t;
	int		i,j,k;
	int		x;
	int		f;
	int		col;
	int		curframe;
	int		ccDataFrame;
//	__int64 fileendpos;

	double	cur_ar_ratio;
//	time_t	ltime;
again:
	logoInfoAvailable = true;
	if (!in_file) {
		Debug(0, "Something went wrong... Exiting...\n");
		exit(22);
	}
	fgets(line, sizeof(line), in_file); // Skip first line
	t = 0.0;
	if (strlen(line) > 85)
		t = ((double)strtol(&line[85], NULL, 10))/100;
	if (t == 25 || t == 29.97 || t == 59.94)
		fps = t;
	if (strlen(line) > 94)
		t = ((double)strtol(&line[94], NULL, 10))/100;
	if (t == 25 || t == 29.97 || t == 59.94)
		fps = t;

	InitComSkip();
	frame_count = 1;
	while (fgets(line, sizeof(line), in_file) != NULL) {
		i = 0;
		x = 0;
		col = 0;
		lineProcessed = false;
		InitializeFrameArray(frame_count);

//		i, frame[i].brightness, frame[i].schange_percent*5, frame[i].logo_present, 
//				frame[i].uniform, frame[i].volume,  frame[i].minY,frame[i].maxY,(int)((frame[i].ar_ratio)*100),
//				(int)(frame[i].currentGoodEdge * 500), frame[i].isblack

		frame[frame_count].minX = 0;
		frame[frame_count].maxX = 0;
		frame[frame_count].hasBright = 0;
		frame[frame_count].dimCount = 0;


		// Split Line Apart
    while (line[i] != '\0' && i < (int)sizeof(line) && !lineProcessed) {
			if (line[i] == ';' || line[i] == ',' || line[i] == '\n') {
				split[x] = '\0';

				// printf("col = %i\t", col);
				switch (col) {
				case 0:
					f = strtol(split, NULL, 10);
					if (f!= frame_count) {
						Debug(0, "Shit!!!!\n");
						exit(23);
					}
					break;

				case 1:
					frame[frame_count].brightness = strtol(split, NULL, 10);
					break;

				case 2:
					frame[frame_count].schange_percent = strtol(split, NULL, 10)/5;
					break;

				case 3:
					frame[frame_count].logo_present = strtol(split, NULL, 10);
					break;

				case 4:
					frame[frame_count].uniform = strtol(split, NULL, 10);
					break;
				case 5:
					frame[frame_count].volume = strtol(split, NULL, 10);
					break;
				case 6:
					frame[frame_count].minY = strtol(split, NULL, 10);
					if (minminY > frame[frame_count].minY) minminY = frame[frame_count].minY;
					break;
				case 7:
					frame[frame_count].maxY = strtol(split, NULL, 10);
					if (maxmaxY < frame[frame_count].maxY) maxmaxY = frame[frame_count].maxY;
					break;
				case 8:
					frame[frame_count].ar_ratio = ((double)strtol(split, NULL, 10))/100;
					break;
				case 9:
					frame[frame_count].currentGoodEdge = ((double)strtol(split, NULL, 10))/500;
					break;
				case 10:
					frame[frame_count].isblack = strtol(split, NULL, 10);
					if (!(frame[frame_count].isblack == 0 || frame[frame_count].isblack == 1))
						old_format = false;
					break;
				case 11:
					frame[frame_count].cutscenematch = strtol(split, NULL, 10);
					if ( frame[frame_count].cutscenematch>0 ) cutscene_nonzero_count++;
					break;
				case 12:
					frame[frame_count].minX = strtol(split, NULL, 10);
					if (minminX > frame[frame_count].minX) minminX = frame[frame_count].minX;
					break;
				case 13:
					frame[frame_count].maxX = strtol(split, NULL, 10);
					if (maxmaxX < frame[frame_count].maxX) maxmaxX = frame[frame_count].maxX;
					break;
				case 14:
					frame[frame_count].hasBright = strtol(split, NULL, 10);
					break;
				case 15:
					frame[frame_count].dimCount = strtol(split, NULL, 10);
					break;


				default:
#ifdef FRAME_WITH_HISTOGRAM
					frame[frame_count].histogram[col - 9] = strtol(split, NULL, 10);
#endif
					break;
				}

				col++;
				x = 0;
				split[0] = '\0';
				if (col > 256 + 9) lineProcessed = true;
			} else {
				split[x] = line[i];
				x++;
			}

			i++;
		}
		frame_count++;
	}

	if (!dump_data_file) {
		sprintf(line, "%s.data", basename);
		dump_data_file = fopen(line, "rb");
	}
	ccDataFrame = 0;


	for (i=0; i < 1000 && i < frame_count; i++)
	{
		if (frame[i].hasBright > 0)
			use_bright = 1;
	}


	FindLogoThreshold();

	height = maxmaxY + minminY;
	width = maxmaxX + minminX;

	last_brightness = frame[1].brightness;
	Debug(8, "CSV file loaded into memory.\n");
	fclose(in_file);
	in_file = NULL;
	black_count = 0;
	logo_block_count = 0;
	black_count = 0;
	schange_count = 0;
	min_brightness_found = 255;
	for (i = 1; i < frame_count; i++) {
		if (i == 33368)
			i = i;
ccagain:
		if (dump_data_file && ccDataFrame == 0) {
			cont = fread(line,8,1,dump_data_file);
			line[8]=0;
			sscanf(line,"%7d:",&ccDataFrame);
//			ccDataFrame = strtol(line,NULL,7);
		}
		if (dump_data_file ) {

		while (cont && ccDataFrame <=i) {

			cont = fread(line,4,1,dump_data_file);
			if (!cont)
				break;
			line[4]=0;
			sscanf(line,"%4d",&ccDataLen);
//			ccDataLen = strtol(line,NULL,4);
			cont = fread(ccData,ccDataLen,1, dump_data_file);
			if (!cont)
				break;
			framenum = ccDataFrame-2;
			if (processCC) ProcessCCData();
			if (output_srt || output_smi) process_block(ccData, (int)ccDataLen);

			ccDataFrame = 0;
			goto ccagain;
		}
			
		}
		if (old_format) {
			if (frame[i].isblack) {
				if (frame[i].brightness <= 5) {
					if (frame[i].brightness == 5) 
						frame[i].isblack = C_a;
					if (frame[i].brightness == 4) 
						frame[i].isblack = C_u;         // Checked
					if (frame[i].brightness == 3) 
						frame[i].isblack = C_s;
					if (frame[i].brightness == 2) 
						frame[i].isblack = C_s;			// Checked
					if (frame[i].brightness == 1) 
						frame[i].isblack = C_u;
					frame[i].brightness = max_avg_brightness + 1;
				}
				else
					frame[i].isblack = C_b;
			}
			else
				frame[i].isblack = 0;
		} else {
//			frame[i].isblack &= C_b;
		}
		if (frame[i].isblack & C_b)
			i = i;

		if (frame[i].isblack & C_r)
			i = i;

		if (frame[i].brightness > 0) {
			brightHistogram[frame[i].brightness]++;

			if (frame[i].brightness < min_brightness_found) min_brightness_found = frame[i].brightness;

			uniformHistogram[(frame[i].uniform / UNIFORMSCALE < 255 ? frame[i].uniform / UNIFORMSCALE : 255)]++;
		}

		if (frame[i].volume >= 0) {
			volumeHistogram[(frame[i].volume/volumeScale < 255 ? frame[i].volume/volumeScale : 255)]++;
		}


		framenum_real = i;

		if (frame[i].maxX == 0) {
			if (i == 1) {
				if (frame[i].ar_ratio < 0.5) {
					if (frame[i].maxY + frame[i].minY < 600)
						width = 720;
					else if (frame[i].maxY + frame[i].minY < 800)
						width = 1200;
					else
						width = 1920;
				} else
					width = (int) ((frame[i].maxY + frame[i].minY) * frame[i].ar_ratio );
			}
			frame[i].maxX = width - 10;
			frame[i].minX = 10;
		} 
		if (i == 1) {
//			if (frame[i].maxX == 0)
//				width = (int) ((frame[i].maxY - frame[i].minY) * frame[i].ar_ratio );
			ProcessARInfoInit(frame[i].minY, frame[i].maxY, frame[i].minX, frame[i].maxX);
		} else {
			ProcessARInfo(frame[i].minY, frame[i].maxY, frame[i].minX, frame[i].maxX);
		}
		frame[i].ar_ratio = last_ar_ratio;


		if (i == 22494)
			i = i;
		if (frame[i].isblack == 1)
			i = i;

		if ((commDetectMethod & RESOLUTION_CHANGE)) {
/* not reliable!!!!!!!!!!!!!!!!!!!!!
			frame[i].isblack &= ~C_r;
			width = frame[i].minX + frame[i].maxX;
			height = frame[i].minY + frame[i].maxY;

			if ((old_width != 0 && abs(width-old_width) > 50) || (old_height != 0 && abs(height - old_height) > 50)) {
				frame[i].isblack |= C_r;
			}	
			old_width = width;
			old_height = height;
*/
		}
		else
			frame[i].isblack &= ~C_r;


		if (commDetectMethod & BLACK_FRAME) {
			// if (frame[i].brightness <= max_avg_brightness && (non_uniformity == 0 || frame[i].uniform < non_uniformity)/* && frame[i].volume < max_volume */ && !(frame[i].isblack & C_b))
			//    frame[i].isblack |= C_b;
			if ((frame[i].isblack & C_b) && frame[i].brightness > max_avg_brightness)
				frame[i].isblack &= ~C_b;

			if (use_bright) {

				if (frame[i].hasBright > 0 && min_hasBright > frame[i].hasBright * 720 * 480 / width / height) min_hasBright = frame[i].hasBright * 720 * 480 / width / height;
				if (frame[i].dimCount > 0 && min_dimCount > frame[i].dimCount * 720 * 480 / width / height) min_dimCount = frame[i].dimCount * 720 * 480 / width / height;

				if (frame[i].brightness <= max_avg_brightness && frame[i].hasBright < maxbright && frame[i].dimCount < (int)(.05 * width * height))
				    frame[i].isblack |= C_b;
			}

			frame[i].isblack &= ~C_u;
			if (!(frame[i].isblack & C_b) && non_uniformity > 0 && frame[i].uniform < non_uniformity && frame[i].brightness < 180/*&& frame[i].volume < max_volume*/ )
				frame[i].isblack |= C_u;
		} else {
			frame[i].isblack &= ~(C_u | C_b);
		}

		if (frame[i].isblack & C_s)
			frame[i].isblack &= ~C_s;

		
		if (commDetectMethod & SCENE_CHANGE && !(frame[i-1].isblack & C_b) && !(frame[i].isblack & C_b)) {
			if (frame[i].brightness > 5 && abs(frame[i].brightness - last_brightness) > brightness_jump) {
				frame[i].isblack |= C_s;
			}
			if (frame[i].brightness > 5)
				last_brightness = frame[i].brightness;
			
			if (frame[i].brightness > 5 && frame[i].schange_percent < 15) {
				frame[i].isblack |= C_s;
			}
		}

		
		if (frame[i].isblack & C_t)
			frame[i].isblack &= ~C_t;

		if (commDetectMethod & CUTSCENE && cutscene_nonzero_count > 0) {
			if (frame[i].cutscenematch < cutscenedelta)
				frame[i].isblack |= C_t;
		}

		if (frame[i].isblack & C_v)
			frame[i].isblack &= ~C_v;


		if (frame[i].isblack) {
			InsertBlackFrame(i,frame[i].brightness,frame[i].uniform,frame[i].volume, (int)frame[i].isblack);

/*
			j = i-volume_slip;
			if (j < 0) j = 0;
			k = i+volume_slip;
			if (k>frame_count) k = frame_count;
			for (x=j; x<k; x++)
				if (frame[x].volume >= 0) 
					if (black[black_count].volume > frame[x].volume)
						black[black_count].volume = frame[x].volume;
//			if (black[black_count].volume < max_volume) frame[i].volume = 1;
*/
		}

		if ((frame[i].schange_percent < 20) && (black[black_count - 1].frame != i)) {
			if (frame[i].brightness < frame[i - 1].brightness * 2) {
				InitializeSchangeArray(schange_count);
				schange[schange_count].percentage = frame[i].schange_percent;
				schange[schange_count].frame = i;
				schange_count++;
			}
		} else if (frame[i].schange_percent < schange_threshold) {

			// Scene Change threshold: original = 91
			InitializeSchangeArray(schange_count);
			schange[schange_count].percentage = frame[i].schange_percent;
			schange[schange_count].frame = i;
			schange_count++;
		}

		if ((commDetectMethod & LOGO) && ((i % (int)(fps * logoFreq)) == 0)) {
			curLogoTest = (frame[i].currentGoodEdge > logo_threshold);
			lastLogoTest = ProcessLogoTest(i, curLogoTest, false);
			frame[i].logo_present = lastLogoTest;
		}
		if (lastLogoTest) frames_with_logo++;

//		if (live_tv && !frame[i].isblack) {
//			BuildCommListAsYouGo();
//		}

	}
	framenum_real = frame_count;
	framesprocessed = frame_count;

	BuildMasterCommList();

	if (output_debugwindow) {
#ifdef DEBUG
		skip_B_frames=0;
#endif
#ifdef DONATORS
		skip_B_frames=0;
#endif
		processCC = 0;
		i = 0;
		printf("Close window when done\n");
		if (ReviewResult()) {
			LoadIniFile();
			goto again;
		}
		//		printf(" Press Enter to close debug window\n");
//		gets(HomeDir);
	}
	exit(0);
}

void OutputCCBlock(long i) {
	if (i > 1) {
		Debug(
			11,
			"%i\tStart - %6i\tEnd - %6i\tType - %s\n",
			i - 2,
			cc_block[i - 2].start_frame,
			cc_block[i - 2].end_frame,
			CCTypeToStr(cc_block[i - 2].type)
		);
	}

	if (i > 0) {
		Debug(
			11,
			"%i\tStart - %6i\tEnd - %6i\tType - %s\n",
			i - 1,
			cc_block[i - 1].start_frame,
			cc_block[i - 1].end_frame,
			CCTypeToStr(cc_block[i - 1].type)
		);
	}

	if (i <= 0) {
		Debug(
			11,
			"%i\tStart - %6i\tEnd - %6i\tType - %s\n",
			i,
			cc_block[i].start_frame,
			cc_block[i].end_frame,
			CCTypeToStr(cc_block[i - 1].type)
		);
	}
}
/*
typedef struct
{
	long	frame;
	char	name[40];
	int		v_chip;
	int		duration;
	int		current;
} XDS_block_info;

XDS_block_info*			XDS_block = NULL;
long					XDS_block_count = 0;
long					max_XDS_block_count;
*/


Init_XDS_block()
{
	if(!XDS_block) {
		max_XDS_block_count = 2000;
		XDS_block = malloc((max_XDS_block_count + 1) * sizeof(XDS_block_info));
		if (XDS_block == NULL) {
			Debug(0, "Could not allocate memory for XDS blocks\n");
			exit(22);
		}
		XDS_block_count = 0;
		XDS_block[XDS_block_count].frame = 0;
		XDS_block[XDS_block_count].name[0] = 0;
		XDS_block[XDS_block_count].v_chip = 0;
		XDS_block[XDS_block_count].duration = 0;
		XDS_block[XDS_block_count].position = 0;
		XDS_block[XDS_block_count].composite1 = 0;
		XDS_block[XDS_block_count].composite2 = 0;
	}
}

Add_XDS_block()
{
	if (XDS_block_count < max_XDS_block_count)
	{
		XDS_block_count++;
		XDS_block[XDS_block_count] = XDS_block[XDS_block_count-1];
		XDS_block[XDS_block_count].frame = framenum;
		frame[framenum].xds = XDS_block_count;

	}
	else
		Debug(0, "Too much XDS data, discarded\n");
}


unsigned char XDSbuffer[40][100];
int lastXDS = 0;
int firstXDS = 1;
int startXDS = 1;
int baseXDS = 0;

char *ratingSystem[4] = { "MPAA", "TPG", "CE", "CF" };

#define MAXXDSBUFFER	1024
void AddXDS(unsigned char hi, unsigned char lo)
{
	static unsigned char XDSbuf[MAXXDSBUFFER];
	static int c = 0;
	int i,j;
	int newXDS = 0;
	Init_XDS_block();
	if (startXDS) {
		if ((hi & 0x70) == 0 && hi != 0x8f) {
			startXDS = 0;
			c = 0;
			baseXDS = hi & 0x0f;;
		} else
			return;
	} else {
		if ((hi & 0x7f) == baseXDS + 1)
			return; // COntinueation code
		if ((hi & 0x70) == 0 && hi != 0x8f) 
			return;
	}
	if ((hi & 0x01) == 0 && (hi & 0x70) == 0x00 && hi != 0x8f)
		return;
	if (hi == 0x86 && (lo == 0x02 || lo == 1))
		return;
	if (c >= MAXXDSBUFFER - 4)
	{
		for (i = 0; i < 256; i++)
			XDSbuf[i]=0;
		c = 0;
		startXDS = 1;
		return;
	}
	XDSbuf[c++] = hi;
	XDSbuf[c++] = lo;
	if (hi == 0x8f) {
		startXDS = 1;
		j = 0;
		for (i = 0; i < c; i++)	
			j += XDSbuf[i];
		if ( (j & 0x7f) != 0) {
			c = 0;
			return;
		}
		for (i = 0; i < lastXDS; i++) {
			if (XDSbuffer[i][0] == XDSbuf[0] && XDSbuffer[i][1] == XDSbuf[1]) {
				j = 0;
				while (j < c) {
					if (XDSbuffer[i][j] != XDSbuf[j]) {
						while (j < 100) {
							XDSbuffer[i][j] = XDSbuf[j];
							j++;
						}
						newXDS = 1;
						break;
					}
					j++;
				}
				break;
			}		
		}
		if (i == lastXDS && !firstXDS) {
			j = 0;
			while (j < 100) {
				XDSbuffer[i][j] = XDSbuf[j];
				j++;
			}
			newXDS = 1;
			lastXDS++;
			i++;
		}
		firstXDS = 0;
		if (newXDS) {
			Debug(10, "XDS[%i]: %2x %2x %2x %2x %2x %2x %2x %2x %2x %2x %2x ", framenum, XDSbuf[0], XDSbuf[1], XDSbuf[2], XDSbuf[3], XDSbuf[4], XDSbuf[5], XDSbuf[6], XDSbuf[7], XDSbuf[8], XDSbuf[9], XDSbuf[10], XDSbuf[11]);

			XDSbuf[c-2] = 0;
			for (i=2; i < c-2; i++)
				XDSbuf[i] &= 0x7f;

			if (XDSbuf[0] == 1) {
				if (XDSbuf[1] == 0x01) {
					Debug(10, "XDS[%i]: Program Start Time %02d:%02d %d/%d\n", framenum, XDSbuf[3] & 0x3f, XDSbuf[2] & 0x3f ,  XDSbuf[5] & 0x1f,  XDSbuf[4] & 0x0f);
				} else if (XDSbuf[1] == 0x02) {
//					Debug(10, "XDS[%i]: Program Length\n", XDSbuf[2] & 0x38, XDSbuf[2] & 0x4f ,  XDSbuf[3] & 0x4f,  XDSbuf[3] & 0xb0);
					Debug(10, "XDS[%i]: Program length %d:%d, elapsed %d:%d:%d.%d\n", framenum, XDSbuf[3] & 0x3f, XDSbuf[2] & 0x3f,  XDSbuf[5] & 0x3f,  XDSbuf[4] & 0x3f ,  XDSbuf[6] & 0x3f);
					if ( (XDSbuf[2] << 8) + XDSbuf[3] != XDS_block[XDS_block_count].duration) {
						Add_XDS_block();
						XDS_block[XDS_block_count].duration = (XDSbuf[3] << 8) + XDSbuf[2];
					}
					if ( (XDSbuf[4] << 8) + XDSbuf[5] != XDS_block[XDS_block_count].position) {
						Add_XDS_block();
						XDS_block[XDS_block_count].position = (XDSbuf[5] << 8) + XDSbuf[4];
					}

				
/*
01155         uint length_min  = xds_buf[2] & 0x3f;
01156         uint length_hour = xds_buf[3] & 0x3f;
01157         uint length_elapsed_min  = 0;
01158         uint length_elapsed_hour = 0;
01159         uint length_elapsed_secs = 0;
01160         if (xds_buf.size() > 6)
01161         {
01162             length_elapsed_min  = xds_buf[4] & 0x3f;
01163             length_elapsed_hour = xds_buf[5] & 0x3f;
01164         }
01165         if (xds_buf.size() > 8 && xds_buf[7] == 0x40)
01166             length_elapsed_secs = xds_buf[6] & 0x3f;
01167 
01168         QString msg = QString("Program Length %1:%2%3 "
01169                               "Time in Show %4:%5%6.%7%8")
01170             .arg(length_hour).arg(length_min / 10).arg(length_min % 10)
01171             .arg(length_elapsed_hour)
01172             .arg(length_elapsed_min / 10).arg(length_elapsed_min % 10)
01173             .arg(length_elapsed_secs / 10).arg(length_elapsed_secs % 10);
01174 
*/				
				
				} else if (XDSbuf[1] == 0x83) {
					if (strcmp(XDS_block[XDS_block_count].name, &XDSbuf[2]) != 0) {
						Add_XDS_block();
						strcpy(XDS_block[XDS_block_count].name, &XDSbuf[2]);
					}
					Debug(10, "XDS[%i]: Program Name: %s\n", framenum, &XDSbuf[2]);  
//		XDS_block[XDS_block_count].name[0] = 0;
				} else if (XDSbuf[1] == 0x04) {
					Debug(10, "XDS[%i]: Program Type: %0x\n", framenum, &XDSbuf[2]);
				} else if (XDSbuf[1] == 0x85) {
					Debug(10, "XDS[%i]: V-Chip: %2x %2x %2x %2x\n", framenum, XDSbuf[2] & 0x38, XDSbuf[2] & 0x4f ,  XDSbuf[3] & 0x4f,  XDSbuf[3] & 0xb0);
					if ( (XDSbuf[2] << 8) + XDSbuf[3] != XDS_block[XDS_block_count].v_chip) {
						Add_XDS_block();
						XDS_block[XDS_block_count].v_chip = (XDSbuf[2] << 8) + XDSbuf[3];
					}

//							XDS_block[XDS_block_count].v_chip = 0;

				} else if (XDSbuf[1] == 0x86) {
					Debug(10, "XDS[%i]: Audio Streams \n", framenum);
				} else if (XDSbuf[1] == 0x07) {
					Debug(10, "XDS[%i]: Caption Stream\n", framenum);
				} else if (XDSbuf[1] == 0x08) {
					Debug(10, "XDS[%i]: Copy Management\n", framenum);
				} else if (XDSbuf[1] == 0x89) {
					Debug(10, "XDS[%i]: Aspect Ratio\n", framenum);
				} else if (XDSbuf[1] == 0x8c) {
					Debug(10, "XDS[%i]: Program Data, Name: %s\n", framenum, &XDSbuf[2]);
				} else if (XDSbuf[1] == 0x0d) {
					Debug(10, "XDS[%i]: Miscellaneous Data: %s\n", framenum, &XDSbuf[2]);
				} else if (XDSbuf[1] == 0x010) {
					Debug(10, "XDS[%i]: Program Description: %s\n", framenum, &XDSbuf[2]);
				} 
				else
					Debug(10, "XDS[%i]: Unknown\n", framenum, &XDSbuf[2]);

			} else if (XDSbuf[0] == 0x85) {
				if (XDSbuf[1] == 0x01) {
					Debug(10, "XDS[%i]: Network Name: %s\n", framenum, &XDSbuf[2]);
				} else if (XDSbuf[1] == 0x02) {
					Debug(10, "XDS[%i]: Network Call Name: %s\n", framenum, &XDSbuf[2]);
				}
				else
					Debug(10, "XDS[%i]: Unknown\n", framenum, &XDSbuf[2]);
			} else if (XDSbuf[0] == 0x0d) {
				Debug(10, "XDS[%i]: Private Data: %s\n", framenum, &XDSbuf[2]);
			} else {
dumpname:
				for (i=0; i < 256; i++) {
					XDSbuf[i] &= 0x7f;
					if (XDSbuf[i] < 0x20)
						XDSbuf[i] = ' ';
					else if (XDSbuf[i] == 0x20)
						XDSbuf[i] = '_';
				}
				Debug(10, "XDS[%i]: %s\n", framenum, XDSbuf);
			}
		}
		for (i = 0; i < 256; i++)
			XDSbuf[i]=0;
		c = 0;
	}
}

void AddCC(int i)
{
	int offset;
	static int		col = 0;
	static int		row = 0;
	bool			tempBool;
	long			current_frame = framenum;
	int hi,lo;

	unsigned char	charmap[0x60] =
	{
		' ',
		'!',
		'"',
		'#',
		'$',
		'%',
		'&',
		'\'',
		'(',
		')',
		'�',
		'+',
		',',
		'-',
		'.',
		'/',
		'0',
		'1',
		'2',
		'3',
		'4',
		'5',
		'6',
		'7',
		'8',
		'9',
		':',
		';',
		'<',
		'=',
		'>',
		'?',
		'@',
		'A',
		'B',
		'C',
		'D',
		'E',
		'F',
		'G',
		'H',
		'I',
		'J',
		'K',
		'L',
		'M',
		'N',
		'O',
		'P',
		'Q',
		'R',
		'S',
		'T',
		'U',
		'V',
		'W',
		'X',
		'Y',
		'Z',
		'[',
		'�',
		']',
		'�',
		'�',
		'�',
		'a',
		'b',
		'c',
		'd',
		'e',
		'f',
		'g',
		'h',
		'i',
		'j',
		'k',
		'l',
		'm',
		'n',
		'o',
		'p',
		'q',
		'r',
		's',
		't',
		'u',
		'v',
		'w',
		'x',
		'y',
		'z',
		'�',
		'�',
		'N',
		'n',
		'?'
	};
	cc.cc1[0] &= 0x7f;
	cc.cc1[1] &= 0x7f;
	if (cc.cc1[0] == 0 && cc.cc1[1] == 0)
		return;
	
	
	current_frame++;
/*
	if ((cc.cc1[0] != 0x14 && cc.cc1[0] < 0x20)) {
		cc.cc1[0] = ' ';
		cc.cc1[1] = 0;
	}
*/


	hi = cc.cc1[0];
	lo = cc.cc1[1];


//	if (hi == ' ' && lo == 'B')
//		hi = hi;


    if (hi>=0x18 && hi<=0x1f)
        hi=hi-8;
    switch (hi)
    {
    case 0x10:
//      if (lo>=0x40 && lo<=0x5f)
//          handle_pac (hi,lo,wb);
        break;
    case 0x11:
        if (lo>=0x20 && lo<=0x2f)
		{
			cc.cc1[0] = 0x20;
			cc.cc1[1] = 0x00;
		}
//          handle_text_attr (hi,lo,wb);
        if (lo>=0x30 && lo<=0x3f)
		{
			cc.cc1[0] = 0x20;
			cc.cc1[1] = 0x00;
//	wrote_to_screen=1;
//          handle_double (hi,lo,wb);
		}
	    if (lo>=0x40 && lo<=0x7f)
		{
//          handle_pac (hi,lo,wb);
			cc.cc1[0] = 0x20;
			cc.cc1[1] = 0x00;
		}
        break;
    case 0x12:
    case 0x13:
        if (lo>=0x20 && lo<=0x3f)
		{
			cc.cc1[0] = 0x20;
			cc.cc1[1] = 0x00;
//          handle_extended (hi,lo,wb);
//			wrote_to_screen=1;
		}
//        if (lo>=0x40 && lo<=0x7f)
//          handle_pac (hi,lo,wb);
        break;
    case 0x14:
    case 0x15:
//        if (lo>=0x20 && lo<=0x2f)
//          handle_command (hi,lo,wb);
//        if (lo>=0x40 && lo<=0x7f)
//          handle_pac (hi,lo,wb);
        break;
    case 0x16:
//        if (lo>=0x40 && lo<=0x7f)
//          handle_pac (hi,lo,wb);
        break;
    case 0x17:
//        if (lo>=0x21 && lo<=0x22)
//           handle_command (hi,lo,wb);
//        if (lo>=0x2e && lo<=0x2f)
//            handle_text_attr (hi,lo,wb);
//        if (lo>=0x40 && lo<=0x7f)
//            handle_pac (hi,lo,wb);
        break;
    }







	if ((cc.cc1[0] >= 0x20) && (cc.cc1[0] < 0x80)) {
		if ((current_cc_type == ROLLUP) || (current_cc_type == PAINTON)) {
			cc_on_screen = true;
		} else if (current_cc_type == POPON) {
			cc_in_memory = true;
		}
		
		Debug(11, "%i:%i) %i:'%c':%x\t", cc_text_count, cc_text[cc_text_count].text_len, i, charmap[cc.cc1[0] - 0x20], cc.cc1[0]);
		cc_text[cc_text_count].text[cc_text[cc_text_count].text_len] = charmap[cc.cc1[0] - 0x20];
		cc_text[cc_text_count].text_len++;
		cc_text[cc_text_count].text[cc_text[cc_text_count].text_len] = '\0';
		if ((cc.cc1[1] >= 0x20) && (cc.cc1[1] < 0x80)) {
			Debug(11, "%i:%i) %i:'%c':%x\t", cc_text_count, cc_text[cc_text_count].text_len, i, charmap[cc.cc1[1] - 0x20], cc.cc1[1]);
			cc_text[cc_text_count].text[cc_text[cc_text_count].text_len] = charmap[cc.cc1[1] - 0x20];
			cc_text[cc_text_count].text_len++;
			cc_text[cc_text_count].text[cc_text[cc_text_count].text_len] = '\0';
			if ((last_cc_type == ROLLUP) || (last_cc_type == PAINTON)) {
				cc_on_screen = true;
			} else if (last_cc_type == POPON) {
				cc_in_memory = true;
			}
		}
	}
	
	if (((!isalpha(cc_text[cc_text_count].text[cc_text[cc_text_count].text_len - 1])) && (cc_text[cc_text_count].text_len > 200)) ||
		(cc_text[cc_text_count].text_len > 245)) {
		cc_text[cc_text_count].end_frame = current_frame - 1;
		cc_text_count++;
		InitializeCCTextArray(cc_text_count);
		cc_text[cc_text_count].start_frame = current_frame;
		cc_text[cc_text_count].text_len = 0;
	}
	
	if ((cc.cc1[0] == 0x14)) {
		if ((cc.cc1[0] == lastcc.cc1[0]) && (cc.cc1[1] == lastcc.cc1[1])) {
			Debug(11, "Double code found\n");
			return;
		}
		
		switch (cc.cc1[1]) {
		case 0x20:
			Debug(11, "Frame - %6i Control Code Found:\tResume Caption Loading\n", current_frame);
			cc_text[cc_text_count].end_frame = current_frame - 1;
			cc_text_count++;
			InitializeCCTextArray(cc_text_count);
			cc_text[cc_text_count].start_frame = current_frame;
			cc_text[cc_text_count].text_len = 0;
			last_cc_type = POPON;
			current_cc_type = POPON;
			AddNewCCBlock(current_frame, current_cc_type, cc_on_screen, cc_in_memory);
			break;
			
		case 0x21:
			// Debug(11, "Frame - %6i Control Code
			// Found:\tBackSpace\n", current_frame);
			break;
			
		case 0x22:
			// Debug(11, "Frame - %6i Control Code
			// Found:\tAlarm Off\n", current_frame);
			break;
			
		case 0x23:
			// Debug(11, "Frame - %6i Control Code
			// Found:\tAlarm On\n", current_frame);
			break;
			
		case 0x24:
			// Debug(11, "Frame - %6i Control Code
			// Found:\tDelete to end of row\n", current_frame);
			break;
			
		case 0x25:
			Debug(11, "Frame - %6i Control Code Found:\tRoll Up Captions 2 row\n", current_frame);
			cc_text[cc_text_count].end_frame = current_frame - 1;
			cc_text_count++;
			InitializeCCTextArray(cc_text_count);
			cc_text[cc_text_count].start_frame = current_frame;
			cc_text[cc_text_count].text_len = 0;
			last_cc_type = ROLLUP;
			current_cc_type = ROLLUP;
			AddNewCCBlock(current_frame, current_cc_type, cc_on_screen, cc_in_memory);
			break;
			
		case 0x26:
			Debug(11, "Frame - %6i Control Code Found:\tRoll Up Captions 3 row\n", current_frame);
			cc_text[cc_text_count].end_frame = current_frame - 1;
			cc_text_count++;
			InitializeCCTextArray(cc_text_count);
			cc_text[cc_text_count].start_frame = current_frame;
			cc_text[cc_text_count].text_len = 0;
			last_cc_type = ROLLUP;
			current_cc_type = ROLLUP;
			AddNewCCBlock(current_frame, current_cc_type, cc_on_screen, cc_in_memory);
			break;
			
		case 0x27:
			Debug(11, "Frame - %6i Control Code Found:\tRoll Up Captions 4 row\n", current_frame);
			cc_text[cc_text_count].end_frame = current_frame - 1;
			cc_text_count++;
			InitializeCCTextArray(cc_text_count);
			cc_text[cc_text_count].start_frame = current_frame;
			cc_text[cc_text_count].text_len = 0;
			last_cc_type = ROLLUP;
			current_cc_type = ROLLUP;
			AddNewCCBlock(current_frame, current_cc_type, cc_on_screen, cc_in_memory);
			break;
			
		case 0x28:
			// Debug(11, "Frame - %6i Control Code
			// Found:\tFlash On\n", current_frame);
			break;
			
		case 0x29:
			Debug(11, "Frame - %6i Control Code Found:\tResume Direct Captioning\n", current_frame);
			cc_text[cc_text_count].end_frame = current_frame - 1;
			cc_text_count++;
			InitializeCCTextArray(cc_text_count);
			cc_text[cc_text_count].start_frame = current_frame;
			cc_text[cc_text_count].text_len = 0;
			last_cc_type = PAINTON;
			current_cc_type = PAINTON;
			AddNewCCBlock(current_frame, current_cc_type, cc_on_screen, cc_in_memory);
			break;
			
		case 0x2A:
			// Debug(11, "Frame - %6i Control Code
			// Found:\tText Restart\n", current_frame);
			break;
			
		case 0x2B:
			// Debug(11, "Frame - %6i Control Code
			// Found:\tResume Text Display\n", current_frame);
			break;
			
		case 0x2C:
			Debug(11, "Frame - %6i Control Code Found:\tErase Displayed Memory\n", current_frame);
			cc_text[cc_text_count].end_frame = current_frame - 1;
			cc_text_count++;
			InitializeCCTextArray(cc_text_count);
			cc_text[cc_text_count].start_frame = current_frame;
			cc_text[cc_text_count].text_len = 0;
			cc_on_screen = false;
			current_cc_type = NONE;
			AddNewCCBlock(current_frame, current_cc_type, cc_on_screen, cc_in_memory);
			break;
			
		case 0x2D:
			// Debug(11, "Frame - %6i Control Code
			// Found:\tCarriage Return\n", current_frame);
			if (cc_text[cc_text_count].text_len > 200) {
				cc_text[cc_text_count].end_frame = current_frame - 1;
				cc_text_count++;
				InitializeCCTextArray(cc_text_count);
				cc_text[cc_text_count].start_frame = current_frame;
				cc_text[cc_text_count].text_len = 0;
			}
			
			cc_text[cc_text_count].text[cc_text[cc_text_count].text_len] = ' ';
			cc_text[cc_text_count].text_len++;
			cc_text[cc_text_count].text[cc_text[cc_text_count].text_len] = '\0';
			Debug(11, "\n");
			break;
			
		case 0x2E:
			Debug(11, "Frame - %6i Control Code Found:\tErase Non-Displayed Memory\n", current_frame);
			
			// cc_text_count++;
			// InitializeCCTextArray(cc_text_count);
			cc_in_memory = false;
			break;
			
		case 0x2F:
			Debug(
				11,
				"Frame - %6i Control Code Found:\tEnd of Caption\tOn Screen - %i\tOff Screen - %i\n",
				current_frame,
				cc_in_memory,
				cc_on_screen
				);
			cc_text[cc_text_count].end_frame = current_frame - 1;
			cc_text_count++;
			InitializeCCTextArray(cc_text_count);
			cc_text[cc_text_count].start_frame = current_frame;
			cc_text[cc_text_count].text_len = 0;
			tempBool = cc_in_memory;
			cc_in_memory = cc_on_screen;
			cc_on_screen = tempBool;
			if (!cc_on_screen) {
				current_cc_type = NONE;
			} else {
				if ((cc_block_count > 0) && (cc_block[cc_block_count].type == NONE)) {
					current_cc_type = last_cc_type;
				}
			}
			
			AddNewCCBlock(current_frame, current_cc_type, cc_on_screen, cc_in_memory);
			break;
			
		default:
			Debug(11, "\nFrame - %6i Control Code Found:\tUnknown code!! - %2X\n", current_frame, cc.cc1[1]);
			if (cc_text[cc_text_count].text_len > 200) {
				cc_text[cc_text_count].end_frame = current_frame - 1;
				cc_text_count++;
				InitializeCCTextArray(cc_text_count);
				cc_text[cc_text_count].start_frame = current_frame;
				cc_text[cc_text_count].text_len = 0;
			}
			
			cc_text[cc_text_count].text[cc_text[cc_text_count].text_len] = ' ';
			cc_text[cc_text_count].text_len++;
			cc_text[cc_text_count].text[cc_text[cc_text_count].text_len] = '\0';
			break;
		}
	}
			
	lastcc.cc1[0] = cc.cc1[0];
	lastcc.cc1[1] = cc.cc1[1];
			
}

extern char field_t;

void ProcessCCData(void) {
	int				i;
	int proceed = 0;
	int is_CC = 0;
	int is_dish = 0;
	int is_GA = 0;
	int cctype = 0;
	int offset;
	char temp[2000];
	char hex[10];
	unsigned char t;
	unsigned char *p;
	bool			cc1First;
	unsigned char	packetCount;

	if (!initialized) return;
	if (verbose >= 12) {
		p = temp;
		for (i = 0; i < ccDataLen; i++) {
			t = ccData[i] & 0x7f;
			if (t == 0x20)
				*p++ = '_';
			else if (t > 0x20 && t < 0x7f)
				*p++ = t;
			else
				*p++ = ' ';
			*p++ = ' ';
			*p++ = ' ';
		}
		*p++ = 0;
		if (ccData[0] == 'G')
			temp[7*3] = '0' + (temp[7*3] & 0x03);
		Debug(10, "CCData for framenum %4i%c, length:%4i: %s\n", framenum, field_t, ccDataLen, temp);

		p = temp;
		for (i = 0; i < ccDataLen; i++) {
			sprintf(hex, "%2x ",ccData[i]);
				*p++ = hex[0];
				*p++ = hex[1];
				*p++ = ' ';
		}
		*p++ = 0;
		Debug(10, "CCData for framenum %4i%c, length:%4i: %s\n", framenum, field_t, ccDataLen, temp);

	}

	if ((char)ccData[0] == 'C' && (char)ccData[1] == 'C' && ccData[2] == 0x01 && ccData[3] == 0xf8) {
		reorderCC = 0;
		packetCount = ccData[4];
		if (packetCount & 0x80) {
			cc1First = true;
		} else {
			cc1First = false;
		}
		offset = 5;
		packetCount = (packetCount & 0x1E) / 2;
		if ((!cc1First) || (packetCount != 15)) {
			Debug(11, "CC Field Order: %i.  There appear to be %i packets.\n", cc1First, packetCount);
		}
		proceed = 1;
		is_CC = 1;
	} else 	if ((char)ccData[0] == 'G' && (char)ccData[1] == 'A' && ccData[2] == '9' && ccData[3] == '4'&& ccData[4] == 0x03) {
		reorderCC = 1;
		packetCount = ccData[5] & 0x1F;
		proceed = ( ccData[5] & 0x40) >> 6;
		offset = 7;
		is_GA = 1;
	} else 	if (ccData[0] == 0x05 && ccData[1] == 0x02) {
		reorderCC = 0;
		proceed = 0;
		offset = 7;
		cctype = ccData[offset++];
		if (cctype == 2) {
			offset++;
			if (ccData[offset] & 0x7f)
				ccDataLen = ccDataLen;
			cc.cc1[0] = ccData[offset++];
			cc.cc1[1] = ccData[offset++];
			AddCC(1);
			cctype = ccData[offset++];
			if (cctype == 4 && ( ccData[offset] & 0x7f) < 32) {
				if (ccData[offset] & 0x7f)
					ccDataLen = ccDataLen;
				cc.cc1[0] = ccData[offset++];
				cc.cc1[1] = ccData[offset++];
				AddCC(1);
			}
			offset += 3;
		} else if (cctype == 4) {
			offset++;
			if (ccData[offset] & 0x7f)
				ccDataLen = ccDataLen;
			cc.cc1[0] = ccData[offset++];
			cc.cc1[1] = ccData[offset++];
			AddCC(1);
			if (ccData[offset] & 0x7f)
				ccDataLen = ccDataLen;
			cc.cc1[0] = ccData[offset++];
			cc.cc1[1] = ccData[offset++];
			AddCC(1);
			offset += 3;
		} else if (cctype == 5) {
			for (i = 0; i < prevccDataLen; i +=2) {
				if (prevccData[i] & 0x7f)
					prevccDataLen = prevccDataLen;
				cc.cc1[0] = prevccData[i];
				cc.cc1[1] = prevccData[i+1];
				AddCC(i/2);
			}
			prevccDataLen = 0;
//			offset += 6;
			cctype = ccData[offset++] & 0x7f;
			cctype = ccData[offset++] & 0x7f;
			if (ccData[offset] & 0x7f)
				ccDataLen = ccDataLen;
			cctype = ccData[offset++] & 0x7f;
			cctype = ccData[offset++] & 0x7f;
			if (ccData[offset] & 0x7f)
				ccDataLen = ccDataLen;
			cctype = ccData[offset++] & 0x7f;
			cctype = ccData[offset++] & 0x7f;
//
			cctype = ccData[offset++];
			offset++;
			prevccDataLen = 0;
			if (ccData[offset] & 0x7f)
				ccDataLen = ccDataLen;
			prevccData[prevccDataLen++] = ccData[offset++];
			prevccData[prevccDataLen++] = ccData[offset++];
			if (cctype == 2) {
				cctype = ccData[offset++];
				if (cctype == 4 && ( ccData[offset] & 0x7f) < 32) {
					if (ccData[offset] & 0x7f)
						ccDataLen = ccDataLen;
					prevccData[prevccDataLen++] = ccData[offset++];
					prevccData[prevccDataLen++] = ccData[offset++];
				}
			} else {
				if (ccData[offset] & 0x7f)
					ccDataLen = ccDataLen;

				prevccData[prevccDataLen++] = ccData[offset++];
				prevccData[prevccDataLen++] = ccData[offset++];
			}
			offset += 3;
		}
		packetCount = cctype / 2;
		is_dish = 1;
	}

	if (proceed)
	{

		for (i = 0; i < packetCount; i++) {
			if (is_CC) {
				if (cc1First) {
					cc.cc1[0] = CheckOddParity(ccData[(i * 6) + offset + 1]) ? ccData[(i * 6) + offset + 1] & 0x7f : 0x00;
					cc.cc1[1] = CheckOddParity(ccData[(i * 6) + offset + 2]) ? ccData[(i * 6) + offset + 2] & 0x7f : 0x00;
				} else {
					cc.cc1[0] = CheckOddParity(ccData[(i * 6) + offset + 4]) ? ccData[(i * 6) + offset + 4] & 0x7f : 0x00;
					cc.cc1[1] = CheckOddParity(ccData[(i * 6) + offset + 5]) ? ccData[(i * 6) + offset + 5] & 0x7f : 0x00;
				}
				AddCC(i);
			}
			if (is_GA) {
				
				if (!(ccData[(i * 3) + offset] & 4) >>2 )
					continue;
				if (ccData[(i * 3) + offset] == 0xfa)
					continue;
				if (ccData[(i * 3) + offset + 1]  == 0x80 && ccData[(i * 3) + offset + 2] == 0x80)
					continue;
				if (ccData[(i * 3) + offset + 1]  == 0x00 && ccData[(i * 3) + offset + 2] == 0x00)
					continue;
		
				cctype = (ccData[(i * 3) + offset] & 3);
//				cc.cc1[0] = CheckOddParity(ccData[(i * 3) + offset + 1]) ? ccData[(i * 3) + offset + 1] & 0x7f : 0x00;
//				cc.cc1[1] = CheckOddParity(ccData[(i * 3) + offset + 2]) ? ccData[(i * 3) + offset + 2] & 0x7f : 0x00;
				cc.cc1[0] = ccData[(i * 3) + offset + 1] & 0x7f;
				cc.cc1[1] = ccData[(i * 3) + offset + 2] & 0x7f;

				if (cctype == 0)
					cctype = cctype;
				if (cctype == 1)
					AddXDS(ccData[(i * 3) + offset + 1], ccData[(i * 3) + offset + 2]);

				if (cctype == 2)
					cctype = cctype;
				if (cctype == 3)
					cctype = cctype;
				if (cctype != 0 && cctype != 1 )
					continue;
				if ( cctype == 0 /* || cctype == 1 */ ) {
//					cc.cc1[0] = ccData[(i * 3) + offset + 1] & 0x7f;
//					cc.cc1[1] = ccData[(i * 3) + offset + 2] & 0x7f;
					AddCC(i);

				} else {
					cc.cc1[0] = 0;
					cc.cc1[1] = 0;
				}
			}
/*
			if (is_dish) {

				if (cctype == 2 || cctype == 4) {
					cc.cc1[0] = ccData[(i * 3) + offset + 1] & 0x7f;
					cc.cc1[1] = ccData[(i * 3) + offset + 2] & 0x7f;
					offset = offset - 1;
					AddCC(i);

				} else
					continue;

			}
*/
		}
	}
}

bool CheckOddParity(unsigned char ch) {
	int				i;
	unsigned char	test = 1;
	int				count = 0;
	for (i = 1; i <= 8; i++) {
		if (ch & test) count++;
		test *= 2;
	}

	if (count % 2) {
		return (true);
	} else {
		return (false);
	}
}

void AddNewCCBlock(long current_frame, int type, bool cc_on_screen, bool cc_in_memory) {
	if (cc_block[cc_block_count].type == type) {
		cc_block[cc_block_count].end_frame = current_frame;
	} else {
		Debug(11, "\nFrame - %6i\t%s captions start\n", current_frame, CCTypeToStr(type));
		if (cc_block[cc_block_count].end_frame == -1) {
			Debug(11, "New cblock found\n");
			cc_block[cc_block_count].end_frame = current_frame - 1;
			cc_block_count++;
			InitializeCCBlockArray(cc_block_count);
			cc_block[cc_block_count].start_frame = current_frame;
			cc_block[cc_block_count].type = type;
			if (cc_block_count > 1) {
				if ((cc_block[cc_block_count - 1].end_frame - cc_block[cc_block_count - 1].start_frame < (int)(fps * 1)) &&
					(cc_block[cc_block_count].type == cc_block[cc_block_count - 2].type) &&
					(cc_block[cc_block_count].type != NONE)) {
					cc_block_count -= 2;
					cc_block[cc_block_count].end_frame = -1;
				}
			}
		} else {
			cc_block_count++;
			InitializeCCBlockArray(cc_block_count);
			cc_block[cc_block_count].start_frame = current_frame;
			cc_block[cc_block_count].type = type;
		}

		OutputCCBlock(cc_block_count);
	}
}

char* CCTypeToStr(int type) {
	if (processCC) {
		switch (type) {
		case NONE:
			sprintf(tempString, "NONE");
			break;

		case ROLLUP:
			sprintf(tempString, "ROLLUP");
			break;

		case PAINTON:
			sprintf(tempString, "PAINTON");
			break;

		case POPON:
			sprintf(tempString, "POPON");
			break;

		case COMMERCIAL:
			sprintf(tempString, "COMMERCIAL");
			break;

		default:
			sprintf(tempString, "%d",type);
			break;
		}
	} else {
		tempString[0]=0; // was: sprintf(tempString, "");
	}

	return (tempString);
}

int DetermineCCTypeForBlock(long start, long end) {
	int type = NONE;
	int i = 0;
	int j = 0;
	int cc_block_first = cc_block_count;
	int cc_block_last = 0;
	int cc_type_count[5] = { 0, 0, 0, 0, 0 };
	while (cc_block[cc_block_first].start_frame > start) cc_block_first--;
	while (cc_block[cc_block_last].end_frame < end) cc_block_last++;
	
	// Look for the PAINTON then POPON pattern that is common in commercials
	for (i = cc_block_first; i <= cc_block_last; i++) {
		if (cc_block[i].type != NONE) {
			if (i > 0) {
				if ((cc_block[i - 1].type == PAINTON) && (cc_block[i].type == POPON)) {
					type = COMMERCIAL;
					break;
				}
			}

			if (i > 1) {
				if ((cc_block[i - 2].type == PAINTON) &&
					(cc_block[i - 1].type == NONE) &&
					(cc_block[i - 1].end_frame - cc_block[i - 1].start_frame <= (fps * 1.5) ) &&
					(cc_block[i].type == POPON)) {
					type = COMMERCIAL;
					break;
				}
			}
		}
	}

	// If no commercial pattern found, find the most common type of CC
	if (type != COMMERCIAL) {
		for (i = start; i <= end; i++) {
			for (j = 0; j < cc_block_count; j++) {
				if ((i > cc_block[j].start_frame) && (i < cc_block[j].end_frame)) {
					cc_type_count[cc_block[j].type]++;
					break;
				}
			}
		}

		type = 0;
		for (i = 0; i < 5; i++) {
			if (cc_type_count[i] > cc_type_count[type]) {
				type = i;
			}
		}
	}
	
	Debug(4, "Start - %6i\tEnd - %6i\tCCF - %2i\tCCL - %2i\tType - %s\n", start, end, cc_block_first, cc_block_last, CCTypeToStr(type));
	
	return (type);
}



void SetARofBlocks(void) {
	int		i, j,k;
	double	sumAR = 0.0;
	int		frameCount = 0;
	if (!(commDetectMethod & AR))
		return;
	k = 0;
	for (i = 0; i < block_count; i++) {
		sumAR = 0.0;
		frameCount = 0; // To prevent divide by zero error
		for (j = cblock[i].f_start + cblock[i].b_head; 
			 j < cblock[i].f_end - (int) cblock[i].b_tail; j++) {
			if ( k < ar_block_count && j >= ar_block[k].end ) 
				k++;
			if (ar_block[k].ar_ratio > 1) {
				sumAR += ar_block[k].ar_ratio;
				frameCount++;
			}
		}
		if (frameCount == 0)
			cblock[i].ar_ratio = 1.0;
		else
			cblock[i].ar_ratio = sumAR / (frameCount);
	}
}



bool ProcessCCDict(void) {
	int		i, j;
	char*	ptr;
	char	phrase[1024];
	bool	goodPhrase = true;
	FILE*	dict = NULL;
	dict = fopen(dictfilename, "r");
	if (dict == NULL) {
		return (false);
	}

	Debug(2, "\n\nStarting to process dictionary\n-------------------------------------\n");
	while (fgets(phrase, sizeof(phrase), dict) != NULL) {
		ptr = strchr(phrase, '\n');
		if (ptr != NULL) *ptr = '\0';
		if (strstr(phrase, "-----") != NULL) {
			goodPhrase = false;
			Debug(3, "Finished with good phrases.  Now starting bad phrases.\n");
			continue;
		}
		// just in case the line is empty
		if (strlen(phrase) < 1) continue;

		Debug(3, "Searching for: %s\n", phrase);
		for (i = 0; i < cc_text_count; i++) {
			if (strstr(_strupr((char*)cc_text[i].text), _strupr((char*)phrase)) != NULL) {
				Debug(2, "%s found in cc_text_block %i\n", phrase, i);
				if (goodPhrase) {
					j = FindBlock((cc_text[i].start_frame + cc_text[i].end_frame) / 2);
					if (j == -1) {
						Debug(1, "There was an error finding the correct cblock for cc text cblock %i.\n", i);
					} else {
						Debug(3, "Block %i score:\tBefore - %.2f\t", j, cblock[j].score);
						cblock[j].score /= dictionary_modifier;
						Debug(3, "After - %.2f\n", cblock[j].score);
					}
				} else {
					j = FindBlock((cc_text[i].start_frame + cc_text[i].end_frame) / 2);
					if (j == -1) {
						Debug(1, "There was an error finding the correct cblock for cc text cblock %i.\n", i);
					} else {
						Debug(3, "Block %i score:\tBefore - %.2f\t", j, cblock[j].score);
						cblock[j].score *= dictionary_modifier;
						Debug(3, "After - %.2f\n", cblock[j].score);
					}
				}
			}
		}
	}

	fclose(dict);
	return (true);
}

int FindBlock(long frame) {
	int i;
	for (i = 0; i < block_count; i++) {
		if ((frame >= cblock[i].f_start) && (frame < cblock[i].f_end)) {
			return (i);
		}
	}

	return (-1);
}

void BuildCommListAsYouGo(void) {
	long		c_start[MAX_COMMERCIALS];
	long		c_end[MAX_COMMERCIALS];
	long		ic_start[MAX_COMMERCIALS];
	long		ic_end[MAX_COMMERCIALS];
	char		filename[255];
	int			commercials = 0;
	int			i;
	int			j;
	int			k;
	int			x;
	int			len;
	int			marked = 0;
	double		remainder;
	double		added;
	bool		oldbreak;
	int local_blacklevel;
	static int	runs = 0;
	bool		useLogo;
	int*		onTheFlyBlackFrame;
	int			onTheFlyBlackCount = 0;
	int			brightness_buffer = 5;
	
	if (framenum_real - lastFrameCommCalculated <= 15 * fps) return;
	
	local_blacklevel = min_brightness_found + brightness_buffer;

	if (local_blacklevel < max_avg_brightness)
		local_blacklevel = max_avg_brightness;

	if ((black[black_count-1].brightness <= local_blacklevel) && 
		/*(black[black_count-1].frame == framenum_real) &&*/
		(framenum_real > lastFrame)) {
		
		lastFrameCommCalculated = framenum_real;

		onTheFlyBlackFrame = calloc(black_count, sizeof(int));
		if (onTheFlyBlackFrame == NULL) {
			Debug(0, "Could not allocate memory for onTheFlyBlackFrame\n");
			exit(8);
		}

		Debug(7, "Building list of all frames with a brightness less than %i.\n", local_blacklevel);
		for (i = 0; i < black_count; i++) {
			if (black[i].brightness <= local_blacklevel) {
				onTheFlyBlackFrame[onTheFlyBlackCount] = black[i].frame;
				onTheFlyBlackCount++;
			}
		}

		useLogo = commDetectMethod & LOGO;

		if ((logo_block_count == -1) || (!logoInfoAvailable)) useLogo = false;
		
		// detect individual commercials from black frames
		for (i = 0; i < onTheFlyBlackCount; i++) {
			for (x = i + 1; x < onTheFlyBlackCount; x++) {
				int gap_length = onTheFlyBlackFrame[x] - onTheFlyBlackFrame[i];
				if (gap_length < min_commercial_size * fps) {
					continue;
				}
				oldbreak = commercials > 0 && ((onTheFlyBlackFrame[i] - c_end[commercials - 1]) < 10 * fps);
				if (gap_length > max_commercialbreak * fps ||
					(!oldbreak && gap_length > max_commercial_size * fps) ||
					(oldbreak && (onTheFlyBlackFrame[x] - c_end[commercials - 1] > max_commercial_size * fps))) {
					break;
				}
				added = gap_length / fps + div5_tolerance;
				remainder = added - 5 * ((int)(added / 5.0));
				if ((require_div5 != 1) || (remainder >= 0 && remainder <= 2 * div5_tolerance)) {
					// look for segments in multiples of 5 seconds
					if (oldbreak) {
						if (CheckFramesForLogo(onTheFlyBlackFrame[x - 1], onTheFlyBlackFrame[x]) && useLogo) {

							c_end[commercials - 1] = onTheFlyBlackFrame[x - 1];
							ic_end[commercials - 1] = x - 1;
							Debug(
								10,
									"Logo detected between frames %i and %i.  Setting commercial to %i to %i.\n",
									onTheFlyBlackFrame[x - 1],
									onTheFlyBlackFrame[x],
									c_start[commercials - 1],
									c_end[commercials - 1]
								);
						} else if (onTheFlyBlackFrame[x] > c_end[commercials - 1] + fps) {
							c_end[commercials - 1] = onTheFlyBlackFrame[x];
							ic_end[commercials - 1] = x;
							Debug(
								5,
								"--start: %i, end: %i, len: %.2fs\t%.2fs\n",
								onTheFlyBlackFrame[i],
								onTheFlyBlackFrame[x],
								(onTheFlyBlackFrame[x] - onTheFlyBlackFrame[i]) / fps,
								(c_end[commercials - 1] - c_start[commercials - 1]) / fps
							);
						}
					} else {
						if (CheckFramesForLogo(onTheFlyBlackFrame[i], onTheFlyBlackFrame[x]) && useLogo) {
							Debug(
								6,
									"Logo detected between frames %i and %i.  Skipping to next i.\n",
									onTheFlyBlackFrame[i],
									onTheFlyBlackFrame[x]
								);
							i = x - 1;	/*Gil*/
							break;
						} else {
							Debug(
								1,
								"\n  start: %i, end: %i, len: %.2fs\n",
								onTheFlyBlackFrame[i],
								onTheFlyBlackFrame[x],
								((onTheFlyBlackFrame[x] - onTheFlyBlackFrame[i]) / fps)
							);
							ic_start[commercials] = i;
							ic_end[commercials] = x;
							c_start[commercials] = onTheFlyBlackFrame[i];
							c_end[commercials++] = onTheFlyBlackFrame[x];
							
							Debug(
								1,
								"\n  start: %i, end: %i, len: %is\n",
								c_start[commercials - 1],
								c_end[commercials - 1],
								(int)((c_end[commercials - 1] - c_start[commercials - 1]) / fps)
								);
						}
					}
					i = x - 1;
					x = onTheFlyBlackCount;
				}
			}
		}
		Debug(1, "\n");


		// print out commercial breaks skipping those that are too small or too large
		if (output_default || output_edl || output_dvrmstb) {
			if (output_default) {
				out_file = fopen(out_filename, "w");
				if (!out_file) {
					Sleep(50L);
					out_file = fopen(out_filename, "w");
					if (!out_file) {
						Debug(0, "ERROR writing to %s\n", out_filename);
						exit(103);
					}
				}
//				fprintf(out_file, "FILE PROCESSING COMPLETE %6li FRAMES AT %4i\n-------------------\n",frame_count-1, (int)(fps*100));
			}
			if (output_edl) {
				sprintf(filename, "%s.edl", basename);
				edl_file = fopen(filename, "wb");
				if (!edl_file) {
					Sleep(50L);
					edl_file = fopen(filename, "wb");
					if (!edl_file) {
						Debug(0, "ERROR writing to %s\n", filename);
						exit(103);
					}
				}		
			}
			dvrmstb_file = 0;
			if (output_dvrmstb) {
				if (outputfilename[0])
					sprintf(filename, "%s\\%s.xml", outputfilename, shortbasename);
				else
					sprintf(filename, "%s.xml", basename);
				dvrmstb_file = fopen(filename, "w");
				if (dvrmstb_file) {
					//			fclose(dvrmstb_file);
					fprintf(dvrmstb_file, "<?xml version=\"1.0\" encoding=\"utf-8\" ?>\n<root>\n");
				} else {
					fprintf(stderr, "%s - could not create file %s\n", strerror(errno), filename);
					exit(6);
				}
			}
			for (i = 0; i < commercials; i++) {
				len = c_end[i] - c_start[i];
				if ((len >= (int)min_commercialbreak * fps) && (len <= (int)max_commercialbreak * fps)) {

					// find the middle of the scene change, max 3 seconds.
					j = ic_start[i];
					while ((j > 0) && ((onTheFlyBlackFrame[j] - onTheFlyBlackFrame[j - 1]) == 1)) {

						// find beginning
						j--;
					}
				
					for (k = j; k < onTheFlyBlackCount; k++) {

						// find end
						if ((onTheFlyBlackFrame[k] - onTheFlyBlackFrame[j]) > (int)(3 * fps)) {
							break;
						}
					}
				
					x = j + (int)((k - j) / 2);
					c_start[i] = onTheFlyBlackFrame[x];
					j = ic_end[i];
					if (j < onTheFlyBlackCount-1) {
						while ((j < onTheFlyBlackCount) && ((onTheFlyBlackFrame[j + 1] - onTheFlyBlackFrame[j]) == 1)) {
							// find end
							j++;
							if (j >= onTheFlyBlackCount-1) break;
						}
					}
					for (k = j; k > 0; k--) {

						// find start
						if (onTheFlyBlackFrame[j] - (onTheFlyBlackFrame[k]) > (int)(3 * fps)) {
							break;
						}
					}
					x = k + (int)((j - k) / 2);
					c_end[i] = onTheFlyBlackFrame[x] - 1;
					Debug(2, "Output: %i - start: %i   end: %i\n", i, c_start[i], c_end[i]);

					commercial_count++;
					if (commercial_count >= MAX_COMMERCIALS) {
						Debug(0, "Insufficient memory to manage live_tv commercials\n");
						exit(8);
					}
					commercial[commercial_count].start_frame = c_start[i] + padding*fps - remove_before*fps;
					commercial[commercial_count].end_frame = c_end[i] - padding*fps + remove_after*fps;
					commercial[commercial_count].length = c_end[i]-2*padding - c_start[i] + remove_before + remove_after;
	
					if (out_file)
						fprintf(out_file, "%li\t%li\n", c_start[i] + padding, c_end[i] - padding);
					if (edl_file)
						fprintf(edl_file, "%.2f\t%.2f\t0\n", (double) max(c_start[i] + padding - edl_offset,0) / fps , (double) max(c_end[i] - padding - edl_offset,0) / fps );
					if (dvrmstb_file)
						fprintf(dvrmstb_file, "  <commercial start=\"%f\" end=\"%f\" />\n", (double) (c_start[i] + padding) / fps , (double) (c_end[i] - padding) / fps);
				}
			}
			if (out_file) fflush(out_file);
			if (out_file) fclose(out_file); out_file = 0;
			if (edl_file) fflush(edl_file);
			if (edl_file) fclose(edl_file); edl_file = 0;
			if (dvrmstb_file) { fprintf(dvrmstb_file, " </root>\n"); fclose(dvrmstb_file); dvrmstb_file = 0; }

			if (output_incommercial) {
				sprintf(filename, "%s.incommercial", basename);
				incommercial_file = fopen(filename, "w");
				if (!incommercial_file) {
					fprintf(stderr, "%s - could not create file %s\n", strerror(errno), filename);
					goto skipit;
				} 
				if(commercial[commercial_count].end_frame > framenum_real - incommercial_frames)
					fprintf(incommercial_file, "1\n");
				else	
					fprintf(incommercial_file, "0\n");
				fclose(incommercial_file);
skipit:;
			}
	
		}			

		free(onTheFlyBlackFrame);
	}

}

double get_fps()
{
	return fps;
}

/*

#define uint32_t unsigned int
int frame_delta = 0;

void calculate_time_difference()
{
	int delta;
	if (pts == 0) return;
	if (apts == 0) return;

	delta = ((int)scr) - ((int)ascr);
	frame_delta = delta;

//	pts = 0;
//	apts = 0;
}

#define DELTACOUNT 16
int deltabuffer[DELTACOUNT];
int deltaptr = 0;
int prev_ptsdelta = 0;

void set_picture_dts(unsigned int p,unsigned int d, unsigned int i_p)
{
	int new_ptsdelta,i, deltasum=0;

	pts = (int)(p*10/0.9/frame_period);
	dts = (int)(d*10/0.9/frame_period);
	new_ptsdelta = scr - pts;
	if (fabs(new_ptsdelta)>5) {
		scr = pts;
		for (i = 0; i < DELTACOUNT; i++)
		{
			deltabuffer[i] = 0;
		}
		return;
	}
	deltabuffer[deltaptr++] = new_ptsdelta;
	deltaptr &= (DELTACOUNT - 1);
	for (i = 0; i < DELTACOUNT; i++)
	{
		deltasum += deltabuffer[i];
	}
	deltasum /=  4;

	if (deltasum > 1) {
		scr = scr + 1;
		for (i = 0; i < DELTACOUNT; i++)
		{
			deltabuffer[i] += 1;
		}
	}	
	if (deltasum < -1) {
		scr = scr - 1;
		for (i = 0; i < DELTACOUNT; i++)
		{
			deltabuffer[i] -= 1;
		}
	}	
	prev_ptsdelta = new_ptsdelta;

}

int adeltabuffer[DELTACOUNT];
int adeltaptr = 0;
int aprev_ptsdelta = 0;
int audio_frame_delta = 0;


void set_audio_dts(unsigned int p,unsigned int d)
{
	int new_ptsdelta,i, deltasum=0, deltadelta;

	apts = (int)(p*10.0/0.9/frame_period);
	adts = (int)(d*10.0/0.9/frame_period);
	new_ptsdelta = ascr - apts;
	deltadelta = new_ptsdelta - aprev_ptsdelta;
	if (abs(deltadelta)>200) {
		ascr = apts;
		for (i = 0; i < DELTACOUNT; i++)
		{
			adeltabuffer[i] = 0;
		}
		aprev_ptsdelta = new_ptsdelta;
		return;
	}
	adeltabuffer[adeltaptr++] = deltadelta;
	adeltaptr &= (DELTACOUNT - 1);
	for (i = 0; i < DELTACOUNT; i++)
	{
		deltasum += adeltabuffer[i];
	}
	deltasum /= DELTACOUNT;

	if (deltasum > 0) {
		ascr = ascr - 1;
		audio_frame_delta = -1;
		for (i = 0; i < DELTACOUNT; i++)
		{
			adeltabuffer[i] -= 1;
		}
	}	
	if (deltasum < 0) {
		ascr = ascr + 1;
		audio_frame_delta = 1;
		for (i = 0; i < DELTACOUNT; i++)
		{
			adeltabuffer[i] += 1;
		}
	}
	aprev_ptsdelta = new_ptsdelta;
}

*/

void set_fps(unsigned int fp)
{
	double old_fps = fps;
	//	if (fps == 0.0) {
		if (fp == 450000)
			fps = (double)900000*30/(double)fp;
		if (fp == 450450)
			fps = (double)900000*30/(double)fp;
		if (fp == 540000)
			fps = (double)900000*30/(double)fp;
		if (fp ==  900900) {
			fps = (double)900000*30/(double)fp;
			sage_framenumber_bug = false;
		}
		if (fp == 1080000){
			fps = (double)900000*30/(double)fp;
			sage_framenumber_bug = false;
		}
//	}
	if (old_fps != fps)
		Debug(1, "Frame Rate set to %5.2f f/s\n", fps);
	frame_period = (int)(1000000.0/fps);
}

#define MAX_SAVED_VOLUMES	10000

static struct {
	int frame;
	int volume;
} volumes[MAX_SAVED_VOLUMES];
static int max_fill = 0;

void SaveVolume (int f,int v)
{
	int i;
	for (i = 0; i < MAX_SAVED_VOLUMES; i++) {
		if (volumes[i].frame ==0) {
			volumes[i].frame = f;
			volumes[i].volume = v;
			if (i > max_fill)
				max_fill = i;
			return;
		}
	}
	Debug (1, "Panic volume buffer\n");
}

int RetreiveVolume (int f)
{
	int i;
	for (i = 0; i <= max_fill; i++) {
		if (volumes[i].frame ==f) {
			volumes[i].frame = 0;
			return(volumes[i].volume);
		}
	}
	return(-1);
}


void set_frame_volume(unsigned int f, int volume)
{
	int i;
	int act_framenum;
	if (!initialized) return;

//	ascr += 1;
	act_framenum = f;

	if (act_framenum > 0) {
		if (framearray) 
			if (act_framenum <= frame_count) {
				if (frame[act_framenum].brightness > 5)
					frame[act_framenum].volume = volume;
				if (volume > 0) {
					volumeHistogram[(volume/volumeScale < 255 ? volume/volumeScale : 255)]++;
				}
			}
			if (act_framenum > frame_count) 
				SaveVolume(act_framenum, volume);
		i = black_count-1;
		while (i > 0 && black[i].frame > act_framenum)
			i--;
		if ( i >= 0 && black[i].frame == act_framenum )
			if (black[i].brightness > 0) black[i].volume = volume;
									// Set the zero above to 5 if you do not want the volume to be updated for uniform frames etc.
	}
//	audio_framenum++;
//	ascr += 1;
}



FILE *dump_audio_file;

void dump_audio_start()
{
	char temp[256];
	if (!output_demux) return;
	if (!dump_audio_file) {
		sprintf(temp, "%s.mp2", basename);
		dump_audio_file = fopen(temp, "wb");
	}
}

void dump_audio (char *start, char *end)
{
	if (!output_demux) return;
	if (!dump_audio_file) return;
	
	fwrite(start, end-start, 1, dump_audio_file);
//	fclose(dump_audio_file);
}

FILE *dump_video_file;

void dump_video_start()
{
	char temp[256];
	if (!output_demux) return;
	if (!dump_video_file) {
		sprintf(temp, "%s.m2v", basename);
		dump_video_file = fopen(temp, "wb");
	}
}
void dump_video (char *start, char *end)
{
	if (!output_demux) return;
	if (!dump_video_file) return;
	fwrite(start, end-start, 1, dump_video_file);
//	fclose(dump_video_file);
}

void close_dump(void)
{
	if (!output_demux) return;
	if (dump_audio_file) {
		fclose(dump_audio_file);
	}
	dump_audio_file = NULL;
	if (dump_video_file) {
		fclose(dump_video_file);
	}
	dump_video_file = NULL;
}



void dump_data(char *start, int length)
{
	char temp[2000];
	int i;
	if (!output_data) return;
	if (!length) return;
	if (!dump_data_file) {
		sprintf(temp, "%s.data", basename);
		dump_data_file = fopen(temp, "wb");
	}
	if (length > 1900)
		return;
	sprintf(temp, "%7d:%4d",framenum_real, length);
	for (i=0; i<length; i++)
		temp[i+12] = start[i] & 0xff;
	fwrite(temp, length+12, 1, dump_data_file);

//	fclose(dump_data_file);
}
