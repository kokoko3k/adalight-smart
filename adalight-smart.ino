/* My adalight script for arduino nano.
 * 
 * It does color correction, step based temporal smoothing,
 * 4bit dithering, averaged window based temporal smoothing 
 * and scene change detection.
 * 
 * Each feature can be switched ON/OFF while the sketch is running
 * by sending simple strings to the serial.
 * 
 * It is Hardly optimized and can sustain more than 50fps
 * while driving 33 leds with all the features enabled.
*/

#pragma GCC optimize ("-O3")
#pragma GCC push_options

#include "FastLED.h"

#define NUM_LEDS 33
#define DATA_PIN 11
#define CLOCK_PIN 13
#define COLOR_ORDER BRG // RGB //GRB
uint8_t prefix[] = {'A', 'd', 'a'}, hi, lo, chk, i;
uint8_t prefix_cfg[] = {'A', 'd', 'c'};	// Magic word for sketch realtime configuration
										// (note that using the same 2 letters as the prefix is intended and needed
#define serialRate 500000
#define dither_steps 4
#define fixmathscale 100  //please don't change it due to some hardcoded things ( eg: mymodulo100())
#define window 5 // Define the smoothing window size


struct FCRGB {
	uint16_t r;
	uint16_t g;
	uint16_t b;
};

CRGB leds[NUM_LEDS];

FCRGB fleds[NUM_LEDS];
FCRGB foldleds[NUM_LEDS];

FCRGB windowed_leds[NUM_LEDS][window];


/* Configuration */

	bool use_step_smoothing = true ;		//Smooth led fades by stepping through intermediate values

	int min_steps = 1;			//1-255: min frames to fade from a color to another when not changing scene (not including window averaged frames)
								//However, setting this to something higher than 1, makes the fades never complete.
	
	int max_steps = 255;		//1-255: max frames to fade from a color to another when not changing scene (not including window averaged frames)

	bool use_window_average = true ;		/* Apart from step based smoothing, this one activates a small averaged window; helps with flickering.
											 * Disable to gain speed. */

	float max_scene_sum = 1630200 ;	/* in my case: (70+170+255-1) * NUM_LEDS * fixmathscale 
									 * where 70,170,255 are r,g,b   color corrected maximum values 
									 * (they are the last value from the gamma ramps). */

	bool scene_change_detection = true;	//Activates the scene change detection that produces fastest fades on scene change.

	float threshold_scene_change = max_scene_sum / 10;	// If the scene changes enough do a fast fade,

	uint16_t steps_to_change_scene = 6 ;				/* use # steps to fade from a scene to another
														 * note that in addition to that, there are
														 * window averaged frames (5 actually) */

	bool disable_fastled_dither = true; 				//Disable fastled dithering bypassing some checks, gaining some speed.

	#define fastled_dither_threshold 256*fixmathscale 						/* Use FastLED dithering when maximum brightness 
																			 * is under that threshold.
																			 * note that it seems to have higher resolution than
																			 * my implementation, but it seems to flicker more too when fps is under 50.
																			 * and fortunately, till now, this sketch can sustain 50fps with 33leds.
																			 * Use: 256*fixmathscale to force FastLED dithering everytime. */


/* Configuration ends here */
 
	/* CONFIGURATION CODES (output the code to the serial to enable/disable functionalities):
	 * Visual feedback is given to the Serial (tail -f /dev/ttyUSB0)
	 * Example in bash:
	 * ( !! stop hyperiond before sending the string !! )
	 * code=B ; timeout 2 sh -c "while true ; do echo -n Adc$code > /dev/ttyUSB0 ; sleep 0.05 ; done"
	 * 
			code	(decimal)	step_smoothing window_average_smoothing scene_detection fastled_dither
			Adc0	0            0              0                        0               0
			Adc1	1            0              0                        0               1
			Adc2	2            0              0                        1               0
			Adc3	3            0              0                        1               1
			Adc4	4            0              1                        0               0
			Adc5	5            0              1                        0               1
			Adc6	6            0              1                        1               0
			Adc7	7            0              1                        1               1
			Adc8	8            1              0                        0               0
			Adc9	9            1              0                        0               1
			AdcA	10           1              0                        1               0
			AdcB	11           1              0                        1               1
			AdcC	12           1              1                        0               0
			AdcD	13           1              1                        0               1
			AdcE	14           1              1                        1               0
			AdcF	15           1              1                        1               1
	*/


unsigned long tstart ;
uint16_t Maximum_found ; 
float fNfactor,newBrightness;
bool new_scene;
uint16_t iSteps;
uint16_t steps_left_to_change_scene;
unsigned long current_scene_sum = 0;
unsigned long old_scene_sum = 0;

//const PROGMEM uint8_t dither_00[] = { 0,0,0,0 }; // 0%  00..13 
const PROGMEM uint8_t dither_25[] = { 0,0,0,1 }; // 25% 14..37
const PROGMEM uint8_t dither_50[] = { 0,1,0,1 }; // 50% 38..63
const PROGMEM uint8_t dither_75[] = { 0,1,1,1 }; // 75% 64..87

//const PROGMEM uint8_t dither6_0[] = { 0,0,0,0,0,0 }; //0%   00..16
const PROGMEM uint8_t dither6_33[] = { 0,1,0,0,1,0 }; //33%  17..40
const PROGMEM uint8_t dither6_50[] = { 0,1,0,1,0,1 }; //50%  41..58
const PROGMEM uint8_t dither6_66[] = { 1,0,1,1,0,1 }; //66%  59..83


// Gamma inizio: 2.6, gamma fine: 2.6, correzione colore: 1, correzione colore completamente attiva in: 1 passi 
const PROGMEM uint16_t gamma_r[] = {
    0, 10, 10, 10, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 13, 16, 
    19, 22, 26, 30, 34, 39, 44, 49, 55, 61, 67, 74, 82, 89, 98, 106, 
    116, 125, 135, 146, 157, 169, 181, 193, 206, 220, 234, 249, 265, 280, 297, 314, 
    332, 350, 369, 388, 408, 429, 451, 473, 495, 519, 543, 567, 593, 619, 645, 673, 
    701, 730, 759, 789, 820, 852, 885, 918, 952, 987, 1022, 1059, 1096, 1133, 1172, 1212, 
    1252, 1293, 1335, 1378, 1421, 1466, 1511, 1557, 1604, 1652, 1700, 1750, 1800, 1852, 1904, 1957, 
    2011, 2066, 2122, 2179, 2236, 2295, 2354, 2415, 2476, 2539, 2602, 2666, 2732, 2798, 2865, 2933, 
    3003, 3073, 3144, 3216, 3289, 3364, 3439, 3515, 3593, 3671, 3750, 3831, 3912, 3995, 4078, 4163, 
    4249, 4336, 4424, 4513, 4603, 4694, 4786, 4880, 4974, 5070, 5167, 5265, 5364, 5464, 5565, 5668, 
    5771, 5876, 5982, 6089, 6197, 6307, 6418, 6529, 6642, 6757, 6872, 6989, 7107, 7226, 7346, 7467, 
    7590, 7714, 7839, 7966, 8093, 8222, 8353, 8484, 8617, 8751, 8886, 9022, 9160, 9299, 9440, 9582, 
    9725, 9869, 10014, 10161, 10310, 10459, 10610, 10762, 10916, 11071, 11227, 11385, 11544, 11704, 11866, 12029, 
    12193, 12359, 12526, 12695, 12865, 13036, 13209, 13383, 13559, 13736, 13914, 14094, 14275, 14458, 14642, 14827, 
    15014, 15203, 15392, 15584, 15776, 15971, 16166, 16363, 16562, 16762, 16964, 17167, 17371, 17577, 17785, 17994, 
    18205, 18417, 18630, 18845, 19062, 19280, 19500, 19721, 19944, 20168, 20394, 20621, 20850, 21081, 21313, 21546, 
    21781, 22018, 22256, 22496, 22738, 22981, 23226, 23472, 23720, 23969, 24220, 24473, 24727, 24983, 25241, 25500
};

// Gamma inizio: 2.6, gamma fine: 2.6, correzione colore: 0.66, correzione colore completamente attiva in: 1 passi 
const PROGMEM uint16_t gamma_g[] = {
    0, 10, 10, 10, 10, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 
    13, 15, 17, 20, 22, 26, 29, 32, 36, 40, 44, 49, 54, 59, 65, 70, 
    76, 83, 89, 96, 104, 111, 119, 128, 136, 145, 155, 164, 175, 185, 196, 207, 
    219, 231, 243, 256, 270, 283, 297, 312, 327, 342, 358, 374, 391, 408, 426, 444, 
    463, 482, 501, 521, 542, 562, 584, 606, 628, 651, 675, 699, 723, 748, 774, 800, 
    826, 853, 881, 909, 938, 967, 997, 1028, 1059, 1090, 1122, 1155, 1188, 1222, 1257, 1292, 
    1327, 1364, 1400, 1438, 1476, 1515, 1554, 1594, 1634, 1676, 1717, 1760, 1803, 1847, 1891, 1936, 
    1982, 2028, 2075, 2123, 2171, 2220, 2270, 2320, 2371, 2423, 2475, 2528, 2582, 2637, 2692, 2748, 
    2804, 2862, 2920, 2978, 3038, 3098, 3159, 3221, 3283, 3346, 3410, 3475, 3540, 3606, 3673, 3741, 
    3809, 3878, 3948, 4019, 4090, 4163, 4236, 4309, 4384, 4459, 4536, 4613, 4690, 4769, 4848, 4928, 
    5009, 5091, 5174, 5257, 5342, 5427, 5513, 5599, 5687, 5775, 5865, 5955, 6046, 6138, 6230, 6324, 
    6418, 6513, 6610, 6707, 6804, 6903, 7003, 7103, 7205, 7307, 7410, 7514, 7619, 7725, 7831, 7939, 
    8048, 8157, 8267, 8379, 8491, 8604, 8718, 8833, 8949, 9065, 9183, 9302, 9421, 9542, 9663, 9786, 
    9909, 10034, 10159, 10285, 10412, 10541, 10670, 10800, 10931, 11063, 11196, 11330, 11465, 11601, 11738, 11876, 
    12015, 12155, 12296, 12438, 12581, 12725, 12870, 13016, 13163, 13311, 13460, 13610, 13761, 13913, 14066, 14220, 
    14376, 14532, 14689, 14848, 15007, 15167, 15329, 15491, 15655, 15820, 15985, 16152, 16320, 16489, 16659, 16830
};

// Gamma inizio: 2.6, gamma fine: 2.6, correzione colore: 0.274, correzione colore completamente attiva in: 1 passi 
const PROGMEM uint16_t gamma_b[] = {
    0, 10, 10, 10, 10, 10, 10, 11, 11, 11, 11, 11, 11, 11, 11, 11, 
    11, 11, 11, 11, 11, 11, 12, 13, 15, 17, 18, 20, 22, 25, 27, 29, 
    32, 34, 37, 40, 43, 46, 50, 53, 57, 60, 64, 68, 72, 77, 81, 86, 
    91, 96, 101, 106, 112, 118, 123, 129, 136, 142, 149, 155, 162, 169, 177, 184, 
    192, 200, 208, 216, 225, 234, 242, 252, 261, 270, 280, 290, 300, 311, 321, 332, 
    343, 354, 366, 377, 389, 402, 414, 427, 439, 453, 466, 480, 493, 507, 522, 536, 
    551, 566, 581, 597, 613, 629, 645, 662, 679, 696, 713, 731, 748, 767, 785, 804, 
    823, 842, 861, 881, 901, 922, 942, 963, 984, 1006, 1028, 1050, 1072, 1095, 1118, 1141, 
    1164, 1188, 1212, 1236, 1261, 1286, 1311, 1337, 1363, 1389, 1416, 1443, 1470, 1497, 1525, 1553, 
    1581, 1610, 1639, 1668, 1698, 1728, 1758, 1789, 1820, 1851, 1883, 1915, 1947, 1980, 2013, 2046, 
    2080, 2114, 2148, 2183, 2218, 2253, 2289, 2325, 2361, 2398, 2435, 2472, 2510, 2548, 2587, 2625, 
    2665, 2704, 2744, 2784, 2825, 2866, 2907, 2949, 2991, 3033, 3076, 3119, 3163, 3207, 3251, 3296, 
    3341, 3386, 3432, 3478, 3525, 3572, 3619, 3667, 3715, 3764, 3812, 3862, 3911, 3961, 4012, 4063, 
    4114, 4165, 4218, 4270, 4323, 4376, 4430, 4484, 4538, 4593, 4648, 4704, 4760, 4816, 4873, 4930, 
    4988, 5046, 5105, 5164, 5223, 5283, 5343, 5404, 5465, 5526, 5588, 5650, 5713, 5776, 5840, 5904, 
    5968, 6033, 6098, 6164, 6230, 6297, 6364, 6431, 6499, 6568, 6636, 6706, 6775, 6845, 6916, 6987
};

/*
// Gamma inizio: 1.5, gamma fine: 2.6, correzione colore: 1
const PROGMEM uint16_t gamma_r[] = {
    0, 6, 17, 32, 49, 67, 88, 110, 133, 158, 183, 210, 237, 265, 294, 324, 
    354, 385, 417, 449, 481, 514, 547, 580, 614, 648, 683, 717, 752, 787, 823, 858, 
    894, 930, 966, 1002, 1039, 1075, 1112, 1149, 1186, 1223, 1260, 1297, 1335, 1372, 1410, 1448, 
    1485, 1523, 1561, 1600, 1638, 1676, 1715, 1753, 1792, 1831, 1870, 1909, 1948, 1987, 2027, 2067, 
    2106, 2146, 2187, 2227, 2268, 2308, 2349, 2391, 2432, 2474, 2516, 2558, 2600, 2643, 2686, 2729, 
    2773, 2817, 2861, 2905, 2950, 2995, 3041, 3087, 3133, 3180, 3227, 3275, 3323, 3371, 3420, 3469, 
    3519, 3570, 3621, 3672, 3724, 3776, 3829, 3883, 3937, 3992, 4047, 4103, 4160, 4217, 4275, 4333, 
    4393, 4453, 4513, 4575, 4637, 4700, 4763, 4828, 4893, 4959, 5026, 5093, 5162, 5231, 5301, 5372, 
    5444, 5517, 5591, 5666, 5742, 5818, 5896, 5974, 6054, 6135, 6216, 6299, 6383, 6467, 6553, 6640, 
    6728, 6817, 6908, 6999, 7092, 7185, 7280, 7376, 7473, 7572, 7671, 7772, 7875, 7978, 8083, 8188, 
    8296, 8404, 8514, 8625, 8738, 8851, 8967, 9083, 9201, 9320, 9441, 9563, 9686, 9811, 9938, 10065, 
    10195, 10325, 10457, 10591, 10726, 10863, 11001, 11140, 11281, 11424, 11568, 11714, 11861, 12010, 12160, 12312, 
    12466, 12621, 12778, 12936, 13096, 13257, 13421, 13585, 13752, 13920, 14090, 14261, 14434, 14608, 14785, 14963, 
    15142, 15324, 15507, 15691, 15877, 16066, 16255, 16447, 16640, 16834, 17031, 17229, 17429, 17631, 17834, 18039, 
    18246, 18454, 18664, 18876, 19090, 19305, 19522, 19741, 19961, 20184, 20407, 20633, 20860, 21089, 21320, 21552, 
    21787, 22022, 22260, 22499, 22740, 22983, 23227, 23473, 23720, 23970, 24221, 24473, 24727, 24983, 25241, 25500
};
 
// Gamma inizio: 1.5, gamma fine: 2.6, correzione colore: 0.66
const PROGMEM uint16_t gamma_g[] = {
    0, 6, 17, 32, 48, 66, 86, 108, 130, 154, 178, 204, 230, 256, 284, 312, 
    340, 368, 397, 427, 456, 486, 516, 546, 577, 607, 638, 668, 699, 730, 761, 792, 
    823, 854, 884, 915, 946, 977, 1008, 1039, 1069, 1100, 1131, 1161, 1192, 1222, 1253, 1283, 
    1313, 1343, 1374, 1404, 1434, 1464, 1494, 1524, 1554, 1584, 1613, 1643, 1673, 1703, 1733, 1762, 
    1792, 1822, 1852, 1881, 1911, 1941, 1971, 2001, 2031, 2061, 2091, 2121, 2152, 2182, 2213, 2243, 
    2274, 2305, 2336, 2367, 2398, 2430, 2461, 2493, 2525, 2557, 2589, 2622, 2655, 2688, 2721, 2754, 
    2788, 2822, 2856, 2891, 2925, 2961, 2996, 3032, 3068, 3104, 3141, 3178, 3215, 3253, 3291, 3330, 
    3369, 3408, 3448, 3488, 3529, 3570, 3611, 3653, 3696, 3738, 3782, 3826, 3870, 3915, 3960, 4006, 
    4052, 4099, 4147, 4195, 4244, 4293, 4343, 4393, 4444, 4495, 4548, 4600, 4654, 4708, 4763, 4818, 
    4874, 4931, 4988, 5046, 5105, 5164, 5225, 5285, 5347, 5409, 5472, 5536, 5601, 5666, 5732, 5799, 
    5867, 5935, 6004, 6074, 6145, 6217, 6289, 6363, 6437, 6512, 6588, 6664, 6742, 6820, 6900, 6980, 
    7061, 7143, 7226, 7310, 7395, 7480, 7567, 7655, 7743, 7833, 7923, 8014, 8107, 8200, 8295, 8390, 
    8486, 8584, 8682, 8781, 8882, 8983, 9086, 9189, 9294, 9399, 9506, 9614, 9723, 9833, 9944, 10056, 
    10169, 10283, 10398, 10515, 10633, 10751, 10871, 10992, 11115, 11238, 11362, 11488, 11615, 11743, 11872, 12002, 
    12134, 12267, 12401, 12536, 12672, 12810, 12948, 13088, 13230, 13372, 13516, 13661, 13807, 13955, 14103, 14253, 
    14405, 14557, 14711, 14866, 15023, 15180, 15340, 15500, 15661, 15824, 15989, 16154, 16321, 16489, 16659, 16830
};

// Gamma inizio: 1.5, gamma fine: 2.6, correzione colore: 0.274
const PROGMEM uint16_t gamma_b[] = {
    0, 6, 17, 31, 47, 65, 85, 106, 127, 150, 173, 197, 221, 246, 272, 297, 
    323, 349, 375, 402, 428, 455, 481, 508, 534, 560, 587, 613, 639, 665, 690, 716, 
    741, 767, 792, 816, 841, 865, 889, 913, 937, 960, 984, 1007, 1029, 1052, 1074, 1096, 
    1118, 1139, 1160, 1181, 1202, 1223, 1243, 1263, 1283, 1303, 1322, 1342, 1361, 1380, 1398, 1417, 
    1435, 1453, 1471, 1489, 1507, 1524, 1541, 1559, 1576, 1593, 1609, 1626, 1643, 1659, 1675, 1692, 
    1708, 1724, 1740, 1756, 1771, 1787, 1803, 1819, 1834, 1850, 1865, 1881, 1896, 1911, 1927, 1942, 
    1958, 1973, 1988, 2004, 2019, 2035, 2050, 2065, 2081, 2096, 2112, 2128, 2143, 2159, 2175, 2191, 
    2206, 2222, 2238, 2255, 2271, 2287, 2303, 2320, 2336, 2353, 2370, 2386, 2403, 2420, 2438, 2455, 
    2472, 2490, 2507, 2525, 2543, 2561, 2579, 2598, 2616, 2635, 2653, 2672, 2691, 2710, 2730, 2749, 
    2769, 2789, 2809, 2829, 2850, 2870, 2891, 2912, 2933, 2954, 2976, 2997, 3019, 3041, 3064, 3086, 
    3109, 3132, 3155, 3178, 3202, 3226, 3250, 3274, 3299, 3323, 3348, 3374, 3399, 3425, 3451, 3477, 
    3504, 3530, 3558, 3585, 3613, 3640, 3669, 3697, 3726, 3755, 3785, 3814, 3844, 3875, 3906, 3937, 
    3968, 4000, 4032, 4064, 4097, 4130, 4164, 4198, 4232, 4267, 4302, 4338, 4374, 4410, 4447, 4485, 
    4522, 4561, 4599, 4639, 4678, 4718, 4759, 4800, 4842, 4884, 4927, 4970, 5014, 5058, 5103, 5149, 
    5195, 5242, 5289, 5337, 5386, 5435, 5485, 5536, 5587, 5639, 5692, 5746, 5800, 5855, 5910, 5967, 
    6024, 6082, 6141, 6201, 6261, 6323, 6385, 6448, 6512, 6577, 6643, 6710, 6778, 6847, 6916, 6987
};
*/
void setup() {
	FastLED.addLeds<WS2811, DATA_PIN, COLOR_ORDER>(leds,NUM_LEDS);
	FastLED.setDither(1); 
	FastLED.setBrightness(255);
	Serial.begin(serialRate);
	Serial.print(F("Ada\n"));
	delay(500);
	rainbow(20);
	Serial.begin(serialRate);
	Serial.print(F("Ada\n"));
}

void rainbow(uint8_t slowness) {
	CRGB start_color=CRGB(127,127,127);
	CRGB end_color=CRGB(0,0,0);
	CRGB a = CRGB::Blue;
	CRGB b = CRGB::Red;
	CRGB c = CRGB::Yellow;
	CRGB d = CRGB::Green;  
	CRGB leds_rainbow[NUM_LEDS];
	fill_gradient_RGB(leds_rainbow,0,a,7,b);
	fill_gradient_RGB(leds_rainbow,8,b,15,c);
	fill_gradient_RGB(leds_rainbow,16,c,23,d);
	fill_gradient_RGB(leds_rainbow,24,d,31,a);
	for (uint8_t j = 0; j < 255; j++) {
		for (uint8_t i = 0; i < NUM_LEDS; i++) {
			leds[i]=blend(start_color,leds_rainbow[i],j);  
		} 
		FastLED.delay(slowness);
		if ( Serial.available() ) slowness = 0;
	}
	for (uint8_t j = 0; j < 255; j++) {
		for (uint8_t i = 0; i < NUM_LEDS; i++) {
			leds[i]=blend(leds_rainbow[i],end_color,j);  
		}
		FastLED.delay(slowness);
		if ( Serial.available() ) slowness = 0;
	}
	LEDS.showColor(CRGB(0, 0, 0));
}


uint32_t divu5(uint32_t n) { 
	//http://www.hackersdelight.org/divcMore.pdf
	uint32_t q, r;
	q = (n >> 1) + (n >> 2);
	q = q + (q >> 4);
	q = q + (q >> 8);
	q = q + (q >> 16);
	q = q >> 2;
	r = n - q*5;
	return q + (r>4) + (r>9);
}

uint32_t div10_16(uint32_t in) {
	//http://www.hackersdelight.org/divcMore.pdf
	// q = in * 0.8;
	uint32_t q = (in >> 1) + (in >> 2);
	q = q + (q >> 4);
	q = q + (q >> 8);
	//q = q + (q >> 16);  // not needed for 16 bit version

	// q = q / 8;  ==> q =  in *0.1;
	q = q >> 3;

	// determine error
	uint32_t  r = in - ((q << 3) + (q << 1));   // r = in - q*10;
	return  q + (r > 9);
}

uint32_t div10_32(uint32_t in) {
	//http://www.hackersdelight.org/divcMore.pdf
	// q = in * 0.8;
	uint32_t q = (in >> 1) + (in >> 2);
	q = q + (q >> 4);
	q = q + (q >> 8);
	q = q + (q >> 16);  // not needed for 16 bit version

	// q = q / 8;  ==> q =  in *0.1;
	q = q >> 3;

	// determine error
	uint32_t  r = in - ((q << 3) + (q << 1));   // r = in - q*10;
	return  q + (r > 9);
}

void array_copy(FCRGB* src, FCRGB* dst, uint16_t len) {
// Function to copy 'len' elements from 'src' to 'dst'
	//memcpy(dst, src, sizeof(src[0])*len);   //0.208ms
	for (uint8_t i = 0; i < NUM_LEDS; i++) {  //0.145ms
		dst[i].r=src[i].r;
		dst[i].g=src[i].g;
		dst[i].b=src[i].b;
	}
}

bool detect_scene_change(float current_scene_sum, float old_scene_sum ) {
 	float diff = current_scene_sum - old_scene_sum;
	float abs_diff=abs(diff);
	return abs_diff > threshold_scene_change  ;
}

void serial_wait_frame_from_hyperion() {
	uint8_t sData;
	for(i = 0; i < sizeof prefix; ++i) {				// wait for first byte of Magic Word
		waitLoop: 
			//Serial.print(F("Wait byte number:")); Serial.println(i);
			while (!Serial.available()) ;;
			sData =  Serial.read(); 
			if( sData == prefix[i] ) continue;	// Check next byte in Magic Word
			if( sData == prefix_cfg[i] ) continue;	// Check next byte in Configuration Magic Word
			i = 0;								// otherwise, start over
		goto waitLoop;
	}
	//Now that we've the full magic word, check if it is from hyperion or for configuration.
	if (sData == prefix[2] ) {
		while (!Serial.available()) ;; hi = Serial.read();	// Hi, Lo, Checksum
		while (!Serial.available()) ;; lo = Serial.read();
		while (!Serial.available()) ;; chk = Serial.read();
		if (chk != (hi ^ lo ^ 0x55)) {						// if checksum does not match go back to wait
			i=0;
			goto waitLoop;
		}
	} else {
		Serial.println(F("Got Magic configuration word: Adc"));
		//do realtime configuration, read next byte.
		sData = Serial.read();
		Serial.print(F("Got configuration byte: ")) ; Serial.println(sData);
		configure_from_serial(sData);
	}
}

void configure_from_serial(char c) {
	uint8_t v;
	if (c >= '0' && c <= '9') 
		v = (byte)(c - '0') ;
			else
		v = (byte)(c-'A'+10) ; 
	
	Serial.print(F("Converted hex to byte value, value=")); Serial.println(v);
	
	disable_fastled_dither = (bitRead(v,0) == 0) ; 
	scene_change_detection = (bitRead(v,1) == 1) ; 
	use_window_average = (bitRead(v,2) == 1) ; 
	use_step_smoothing = (bitRead(v,3) == 1) ; 

	Serial.println(F("Active features:"));
	if ( ! disable_fastled_dither ) Serial.println(F(" [v] FastLED dither"));
	if ( scene_change_detection )   Serial.println(F(" [v] scene_change_detection"));
	if ( use_window_average )       Serial.println(F(" [v] use_window_average"));
	if ( use_step_smoothing )       Serial.println(F(" [v] use_step_smoothing"));
}

void apply_floor(FCRGB pleds[],uint16_t myfloor){
	int myfloor_new;
	uint16_t mymax,r,g,b;
	for (uint8_t i = 0; i < NUM_LEDS; i++) {
		/*Enforce a minimum value for the components.
		* That minimum value becomes lower as the maximum component value
		* becomes higher.
		* That way, pure blacks becomes "pure floor", but we don't loose saturation
		* at brighter colors.
		*/
		mymax=max3(pleds[i].r,pleds[i].g,pleds[i].b);
		myfloor_new = myfloor - (mymax - myfloor) ;
		if (myfloor_new < 0) myfloor_new = 0;
		if (myfloor_new > myfloor) myfloor_new = myfloor;
		if (pleds[i].r < myfloor) {pleds[i].r = myfloor_new;}
		if (pleds[i].g < myfloor) {pleds[i].g = myfloor_new;}
		if (pleds[i].b < myfloor) {pleds[i].b = myfloor_new;}
	}
}

void read_leds_from_hyperion(CRGB pleds[]) {
	for (uint8_t i = 0; i < NUM_LEDS; i++) {
		while(!Serial.available()); pleds[i].r = Serial.read();
		while(!Serial.available()); pleds[i].g = Serial.read();
		while(!Serial.available()); pleds[i].b = Serial.read();	
	}
}

void color_correct(CRGB source_leds[], FCRGB dest_fleds[]) {
	for (uint8_t i = 0; i < NUM_LEDS; i++) {
		dest_fleds[i].r = pgm_read_word( gamma_r+source_leds[i].r) ;
		dest_fleds[i].g = pgm_read_word( gamma_g+source_leds[i].g) ;
		dest_fleds[i].b = pgm_read_word( gamma_b+source_leds[i].b) ;
	}
}

unsigned long scene_sum(FCRGB pfleds[]){
	unsigned long out = 0;
	for (uint8_t i = 0; i < NUM_LEDS; i++) {
		out = out + pfleds[i].r + pfleds[i].g +pfleds[i].b;  
	}
	return out;
}

uint16_t max3(uint16_t a, uint16_t b, uint16_t c) {
	if ( a >=b ) {
		if ( a >= c ) { 
			return a ;
		}
	}
	if ( b > a ) {
		if (b > c ) { return b; }
	}
	return c ; 
}

uint16_t fsmooth_value_step(uint16_t fStart, uint16_t fEnd, uint8_t ipSteps ){
	int fdiff = fStart - fEnd;
	uint16_t abs_diff = abs(fdiff);
	uint16_t fstep = abs_diff/ipSteps; 
	if ( abs_diff < fstep ) { return fEnd ;}
	
	if (fStart > fEnd) {
			return ( fStart - fstep );
		} else {
			return ( fStart + fstep );   
	}
}

uint16_t new_iSteps(FCRGB src, FCRGB dest) {
	uint16_t out;
	/* Given a source and a destination color, returns the step number to be used to fade.
	 * The step number is the maximum difference found between 2 components.
	 */
	if (max_steps == min_steps) {return max_steps;}
	
	int diff_r,diff_g,diff_b;
	uint16_t abs_diff_r,abs_diff_g,abs_diff_b;

	diff_r=src.r-dest.r    ; diff_g=src.g-dest.g    ; diff_b=src.b-dest.b;
	abs_diff_r=abs(diff_r) ; abs_diff_g=abs(diff_g) ; abs_diff_b=abs(diff_b);
	out = ( max3(abs_diff_r,abs_diff_g,abs_diff_b) / fixmathscale );
	if (out > max_steps) {return max_steps;}
	if (out < min_steps) {return min_steps;}
	return out;
}

void smooth_leds(FCRGB pfOldLeds[], FCRGB pfLeds[]){
	Serial.println(F("Smoothing"));
	bool changing_scene = (steps_left_to_change_scene > 0);
	for (uint8_t i = 0; i < NUM_LEDS; i++) {
		if (changing_scene) {
				iSteps = steps_left_to_change_scene;
			} else {
				iSteps=new_iSteps(pfOldLeds[i],pfLeds[i]) ;
		}
		if (iSteps > 0) {
			pfLeds[i].r = fsmooth_value_step(pfOldLeds[i].r,pfLeds[i].r,iSteps) ;
			pfLeds[i].g = fsmooth_value_step(pfOldLeds[i].g,pfLeds[i].g,iSteps) ;
			pfLeds[i].b = fsmooth_value_step(pfOldLeds[i].b,pfLeds[i].b,iSteps) ;
			}
	}
}

uint16_t find_maximum( FCRGB pleds[] ) {
	//Find the maximum of the strip
	//Warning!: This does not take color correction into account.
	uint16_t m,fR,fG,fB;
	m = 0;
	for (uint8_t i = 0; i < NUM_LEDS; i++) {
		fR = pleds[i].r;
		if ( fR > m ) { m = fR ;}
		fG = pleds[i].g;
		if ( fG > m ) { m = fG ;}
		fB = pleds[i].b;
		if ( fB > m ) { m = fB ;}
	}
	return (m);
}

uint16_t mymodulo100(uint16_t value) {
	//Faster version than "%" 
	if (value > 20000) { value = value - 20000 ; }  
	if (value > 10000) { value = value - 10000;} 
	if (value > 5000) { value = value - 5000 ;}  
	if (value > 2500) { value = value - 2500 ;} 
	if (value > 1200) { value = value - 1200 ;} 
	if (value > 600) { value = value - 600 ;} 
	if (value > 300) { value = value - 300 ;} 
	if (value > 200) { value = value - 200 ;} 
	if (value > 100) { value = value - 100 ;} 
	return value;
}


/*uint8_t dithered(uint16_t in_value, byte step){
	byte fractional = mymodulo100(in_value);
	byte integer = (in_value - fractional) / fixmathscale ;			
	if ( fractional <= 13)	{ return integer  ; }
	if ( fractional <= 37)	{ return integer + pgm_read_byte_near(dither_25 + step); }
	if ( fractional <= 63)	{ return integer + pgm_read_byte_near(dither_50 + step); }
	if ( fractional <= 87)	{ return integer + pgm_read_byte_near(dither_75 + step); }
							  return integer + 1;
}*/


uint8_t dithered(uint16_t in_value, byte step){
	byte fractional = mymodulo100(in_value);
	byte integer = (in_value - fractional) / fixmathscale ;			
	if ( fractional < 13)	{ return integer  ; }
	if ( fractional < 37)	{ return integer + pgm_read_byte_near(dither_25 + step); }
	if ( fractional < 63)	{ return integer + pgm_read_byte_near(dither_50 + step); }
	if ( fractional < 87)	{ return integer + pgm_read_byte_near(dither_75 + step); }
							  return integer + 1;
}


/*uint8_t dithered_6bit(uint16_t in_value, byte step){
	byte fractional = mymodulo100(in_value);
	byte integer = (in_value - fractional) / fixmathscale ;			
	if ( fractional < 16)	{ return integer  ; }
	if ( fractional < 40)	{ return integer + pgm_read_byte_near(dither6_33 + step); }
	if ( fractional < 58)	{ return integer + pgm_read_byte_near(dither6_50 + step); }
	if ( fractional < 83)	{ return integer + pgm_read_byte_near(dither6_66 + step); }
							  return integer + 1;
}*/

void make_dithered_leds(FCRGB source_fleds[],CRGB dithered_leds[], byte step) {
	for (uint8_t i = 0; i < NUM_LEDS; i++) {
		dithered_leds[i].r = dithered(source_fleds[i].r,step);
		dithered_leds[i].g = dithered(source_fleds[i].g,step);
		dithered_leds[i].b = dithered(source_fleds[i].b,step);
		//offset the step between adiacent pixels to mitigate flickering due to dithering.
		//step = step + 1 ; 
		//if ( step == dither_steps ) { step = 0 ;}
	}
}

void show_step_dithering() {
	/* Useless?
	for (uint8_t i = 0; i < NUM_LEDS; i++) {
		leds[i].r = (fleds[i].r) / fixmathscale ;
		leds[i].g = (fleds[i].g) / fixmathscale ;
		leds[i].b = (fleds[i].b) / fixmathscale ;
	}
	*/

	FastLED.setBrightness(255);

	make_dithered_leds(fleds,leds,0); 	FastLED.show() ;
	make_dithered_leds(fleds,leds,1); 	FastLED.show() ;
	make_dithered_leds(fleds,leds,2); 	FastLED.show() ;
	make_dithered_leds(fleds,leds,3); 	FastLED.show() ;
}

void make_averaged_leds(FCRGB new_readings[],FCRGB averaged[]) {
	static uint8_t k;
	unsigned long sum_r,sum_g,sum_b ;
	float window_inv = 1.00/window;
	for (uint8_t i = 0; i < NUM_LEDS; i++) {
		windowed_leds[i][k].r = new_readings[i].r;
		windowed_leds[i][k].g = new_readings[i].g;
		windowed_leds[i][k].b = new_readings[i].b;

		sum_r=0;
		sum_g=0;
		sum_b=0;
		
		for (uint8_t w = 0 ; w < window ; w++) {
			sum_r = sum_r + windowed_leds[i][w].r;
			sum_g = sum_g + windowed_leds[i][w].g;
			sum_b = sum_b + windowed_leds[i][w].b;			
		}

		averaged[i].r = divu5(sum_r) ;  
		averaged[i].g = divu5(sum_g) ; 
		averaged[i].b = divu5(sum_b) ;
	}

	k = k + 1 ; //Move the index where next value will go
	if ( k > ( window-1 ) ) { k = 0;}
}



void loop() {
	tstart=millis();	
	serial_wait_frame_from_hyperion();
	read_leds_from_hyperion(leds);

	color_correct(leds,fleds); 

	//apply_floor(fleds,100);
	
	if (scene_change_detection) {
		current_scene_sum = scene_sum(fleds);
		new_scene = detect_scene_change(current_scene_sum,old_scene_sum);
		if (new_scene) {
			if (steps_left_to_change_scene == 0) steps_left_to_change_scene = steps_to_change_scene+1;
			Serial.print(F("new scene!"));
		}
		if (steps_left_to_change_scene > 0) { steps_left_to_change_scene -- ; }
	}
																						
	if (use_step_smoothing) { smooth_leds(foldleds,fleds); }  //2.2ms

	if (scene_change_detection) {old_scene_sum=scene_sum(fleds);}
	
	array_copy(fleds,foldleds,NUM_LEDS);  //<-memorizza i led attuali come led prcedenti

	if (use_window_average) { make_averaged_leds(foldleds,fleds);} //1.65 ms
	
	//Show time
	
	//If forced, use step dithering and skip the decision between it and fastled dithering
	if ( disable_fastled_dither == true ) {
		show_step_dithering();
		goto past_show;
	}

	Maximum_found=find_maximum(fleds) ;

	if (Maximum_found < (2*fixmathscale)) {Maximum_found = (2*fixmathscale);}	//	<-- imposta il setbrightness minimo, è necessario che sia maggiore di 1, 
																				//  <-- affinchè il dithering funzioni, da vedere come si comporta con valori maggiori tipo 10, 20..
	if (Maximum_found < fastled_dither_threshold) {

		newBrightness = (float)Maximum_found/fixmathscale;
		fNfactor = (255 / newBrightness); //fNfactor is in range 0..25500
		//normalize the strip

		for (uint8_t i = 0; i < NUM_LEDS; i++) {
			leds[i].r = (fleds[i].r * fNfactor) / fixmathscale ;
			leds[i].g = (fleds[i].g * fNfactor) / fixmathscale ;
			leds[i].b = (fleds[i].b * fNfactor) / fixmathscale ;
		}

		FastLED.setBrightness(newBrightness);
		FastLED.show() ;
		FastLED.show() ;
		FastLED.show() ;
		FastLED.show() ;
		
			} else {
		show_step_dithering();
	}

	past_show:
	//Serial.println(leds[0].r);
	Serial.print(F("tot ")) ; Serial.println(millis()-tstart);
}

