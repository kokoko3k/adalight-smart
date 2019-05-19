/* Veloce, quasi perfetto,
 * unico neo è che passa "troppo tempo" prima dell'inizio del dither,
 * probabilmente perchè ha tempo disponibile per fare altri dithering!
*/


#include "FastLED.h"
#define NUM_LEDS 33
#define DATA_PIN 3
#define CLOCK_PIN 13
#define COLOR_ORDER RGB //GRB
uint8_t prefix[] = {'A', 'd', 'a'}, hi, lo, chk, i;
#define serialRate 500000
#define dither_steps 4
#define input_fps 50
#define ms_needed_to_show 4

#define fixmathscale 100

//mydebug on:
#define mydebugln(a) (Serial.println(a))
#define mydebug(a) (Serial.print(a))

//mydebug off:
//#define mydebugln(a) 
//#define mydebug(a) 

struct FCRGB {
	int r;
	int g;
	int b;
};

CRGB leds[NUM_LEDS];
CRGB dither_leds[NUM_LEDS];

FCRGB fleds[NUM_LEDS];
FCRGB foldleds[NUM_LEDS];

int dither_table_r[NUM_LEDS][dither_steps];
int dither_table_g[NUM_LEDS][dither_steps];
int dither_table_b[NUM_LEDS][dither_steps];

/* Configurazione */

float max_scene_sum = 1630200 ;  //(70+170+255-1) * NUM_LEDS * fixmathscale; //70, 170 e 255 sono le correzioni colore
float threshold_scene_change = max_scene_sum / 10 ; //max_scene_sum/10; //se la scena cambia abbastanza, fai un fade istantaneo. metti max_scene_sum per disabilitare.
int steps_to_change_scene = 6; //use # steps to fade from a scene to another
/* Fine */

bool new_scene;
int iSteps;
int steps_left_to_change_scene;
unsigned long current_scene_sum = 0;
unsigned long old_scene_sum = 0;
byte total_cycle_time_ms = 1000/input_fps;
byte dither_every_ms = total_cycle_time_ms/dither_steps ; 
unsigned long hyperion_read_time = millis() ;


// Gamma inizio: 2.6, gamma fine: 2.6, correzione colore: 1, correzione colore completamente attiva in: 1 passi 
const PROGMEM unsigned int gamma_r[] = {
    0, 0, 0, 0, 1, 1, 1, 2, 3, 4, 6, 7, 9, 11, 13, 16, 
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
const PROGMEM unsigned int gamma_g[] = {
    0, 0, 0, 0, 0, 1, 1, 1, 2, 3, 4, 5, 6, 7, 9, 11, 
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
const PROGMEM unsigned int gamma_b[] = {
    0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 2, 2, 2, 3, 4, 4, 
    5, 6, 7, 8, 9, 11, 12, 13, 15, 17, 18, 20, 22, 25, 27, 29, 
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

void array_copy(FCRGB* src, FCRGB* dst, int len) {
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

byte r,g,b;
void serial_wait_frame_from_hyperion(bool infinite) {
	int tries=0;
	for(i = 0; i < sizeof prefix; ++i) {				// wait for first byte of Magic Word
		waitLoop: while (!Serial.available()) ;;
		if(prefix[i] == Serial.read()) continue;		// Check next byte in Magic Word
		i = 0;											// otherwise, start over
		//mydebugln("Wrong Hyperion Magic word.");
		tries++;
		goto waitLoop;
	}
	while (!Serial.available()) ;; hi = Serial.read();	// Hi, Lo, Checksum
	while (!Serial.available()) ;; lo = Serial.read();
	while (!Serial.available()) ;; chk = Serial.read();
	if (chk != (hi ^ lo ^ 0x55)) {						// if checksum does not match go back to wait
		i=0;
		//mydebugln("Checksum bad, retrying");
		tries++;
		goto waitLoop;
	}
	//mydebug("Got magic Word after tries: "); mydebugln(tries);
}

void read_leds_from_hyperion(CRGB pleds[]) {
	for (uint8_t i = 0; i < NUM_LEDS; i++) {
		while(!Serial.available()); r = Serial.read();
		while(!Serial.available()); g = Serial.read();
		while(!Serial.available()); b = Serial.read();	
		pleds[i].r = r; pleds[i].g = g; pleds[i].b = b; 
	}
}

void color_correct(CRGB source_leds[], FCRGB dest_fleds[]) {
	for (uint8_t i = 0; i < NUM_LEDS; i++) {
		dest_fleds[i].r = pgm_read_word( gamma_b+source_leds[i].r) ;
		dest_fleds[i].g = pgm_read_word( gamma_r+source_leds[i].g) ;
		dest_fleds[i].b = pgm_read_word( gamma_g+source_leds[i].b) ;
	}
}

unsigned long scene_sum(FCRGB pfleds[]){
	unsigned long out = 0;
	for (uint8_t i = 0; i < NUM_LEDS; i++) {
		out = out + pfleds[i].r + pfleds[i].g +pfleds[i].b;  
	}
	return out;
}

int fsmooth_value_step(int fStart, int fEnd, int ipSteps ){
	int fdiff = fStart - fEnd;
	int abs_diff = abs(fdiff);
	int fstep = abs_diff/ipSteps;  
	if ( abs_diff > fstep ) {
		if (fStart > fEnd) {
				return ( fStart - fstep );
			} else {
				return ( fStart + fstep );   
		}
		} else {
		return fEnd ;
	}    
}

void set_dither(uint8_t k,int cdither[][4],int bValue,int a,int b,int c,int d){
	cdither[k][0]=bValue+a;
	cdither[k][1]=bValue+b;
	cdither[k][2]=bValue+c;
	cdither[k][3]=bValue+d;	
}

/*
 * Questi threshold sono i margini per passare da uno pattern
 * di dithering all'altro.
 * Il pattern dipende dalla parte frazionaria del numero float.
 * Ma dal momento che non usiamo più i float, ma interi * fixmathscale
 * creiamo dei threshold che siano scalati da [0.00..0.00] a [0..99]
*/

int threshold_step1 = (2 * (fixmathscale/ 10));
int threshold_step2 = (4 * (fixmathscale/ 10));
int threshold_step3 = (6 * (fixmathscale/ 10));
int threshold_step4 = (8 * (fixmathscale/ 10));

int I = fixmathscale ;
void create_dither_tables_component(int pfV,uint8_t j,int current_dither_table[][4]){
	int bV;
	int fractional ;
	fractional = pfV % fixmathscale ;
	bV = pfV - fractional ;;
	
	if ( fractional <= threshold_step1) { set_dither(j,current_dither_table,bV,0,0,0,0); return ; }
	if ( fractional <= threshold_step2) { set_dither(j,current_dither_table,bV,0,0,0,I); return  ; }
	if ( fractional <= threshold_step3) { set_dither(j,current_dither_table,bV,0,I,0,I); return  ; }
	if ( fractional <= threshold_step4) { set_dither(j,current_dither_table,bV,0,I,I,I); return  ; }
									      set_dither(j,current_dither_table,bV,0,0,0,0);
}

void create_dither_tables(FCRGB pfleds[]) {
	for (uint8_t i = 0; i < NUM_LEDS; i++) {
		create_dither_tables_component(pfleds[i].r,i,dither_table_r);
		create_dither_tables_component(pfleds[i].g,i,dither_table_g);
		create_dither_tables_component(pfleds[i].b,i,dither_table_b);
	}
}

unsigned long previous_dithering_time=0;
bool step_dithering(bool debug,byte mark,bool force) {
	
	static byte current_step;
	int time_diff = millis()-previous_dithering_time;
	if (force) {goto do_dither;}

	if ((millis()-hyperion_read_time) > (total_cycle_time_ms - ms_needed_to_show)) {
			//previous_dithering_time=millis();
			//mydebug(mark) ; mydebugln(" have to skip!") ; 
			return false;
	}

	// se non è passato abbastanza tempo dal precedente dithering, esci.
	if (time_diff < dither_every_ms) { 
		if (debug) { mydebug(mark) ;  mydebugln(" Delaying dither"); }
		return true ; 
	}


	do_dither:
	if (current_step == dither_steps) {current_step = 0;}
	for (uint8_t i = 0; i < NUM_LEDS; i++) {
			dither_leds[i].r = (dither_table_r[i][current_step]) / fixmathscale;
			dither_leds[i].g = (dither_table_g[i][current_step]) / fixmathscale;
			dither_leds[i].b = (dither_table_b[i][current_step]) / fixmathscale;
	}
	previous_dithering_time=millis();
	FastLED.show();

									
									//mydebug("Showing step #") ; mydebug(current_step) ; mydebug(" after ms: ") ; mydebugln(time_diff);
									
									//mydebug (String(mark) + String(" step #") );//+ current_step + " r: " + dither_leds[i].r + " Showing step after ms: " + time_diff);
									//mydebug(mark) ; mydebug(": step #"); mydebug(current_step) ; mydebug(" r: ") ; mydebug(dither_leds[i].r) ; mydebug(" after ms: ") ; mydebugln(time_diff); 
									//mydebug(" after ms: ") ; mydebugln(time_diff); 
									
									
									

	current_step++;

	return true;
}

void setup() {
	FastLED.addLeds<WS2811, DATA_PIN, RGB>(dither_leds,NUM_LEDS);
																																				/*FastLED.setMaxRefreshRate(999); /* <----------- OCCHIO !
																																					                                  * dicono che se si aggiornano
																																					                                  * i led con intervallo
																																					                                  * inferiore a 2.5ms, si rompono
																																					                                  * per questo fastled di base
																																					                                  * metter un delay dopo lo show
																																					                                  * ma per me questo non va bene
																																					                                  * quindi gestisco io il delay
																																					                                  * nella funzione che fa lo show
																																													 */
	FastLED.setDither(0); // turn off FastLED dithering
	FastLED.setBrightness(255);
	delay(500);
 	LEDS.showColor(CRGB(0, 0, 0))	;	delay(100);
	LEDS.showColor(CRGB(128, 0, 0))	;	delay(100);
	LEDS.showColor(CRGB(0, 0, 0))	;delay(100);
	LEDS.showColor(CRGB(0, 0, 0)) ;delay(100);
	Serial.begin(serialRate);
	mydebug("Ada\n");
}

int max3(int a, int b, int c) {
	if ( a > b ) {
		if ( a > c ) { return a ;}
	}
	if ( b > a ) {
		if (b > c ) { return b; }
	}
	return c ; 
}

int new_iSteps(FCRGB src, FCRGB dest) {
	//dati due colori, restituisce il numero di step da usare per il fade
	//il numero di step coincide con la differenza intera massima che c'è tra due componenti.
	int diff_r,diff_g,diff_b;
	int abs_diff_r,abs_diff_g,abs_diff_b;

	diff_r=src.r-dest.r    ; diff_g=src.g-dest.g    ; diff_b=src.b-dest.b;
	abs_diff_r=abs(diff_r) ; abs_diff_g=abs(diff_g) ; abs_diff_b=abs(diff_b);
	return ( max3(abs_diff_r,abs_diff_g,abs_diff_b) / fixmathscale );
}


int fsmooth_value_step(int fStart, int fEnd, uint8_t ipSteps ){
	int fdiff = fStart - fEnd;
	int abs_diff = abs(fdiff);
	int fstep = abs_diff/ipSteps; 
	if ( abs_diff <= fstep ) { return fEnd ;}
	
	if (fStart > fEnd) {
			return ( fStart - fstep );
		} else {
			return ( fStart + fstep );   
	}

}

void smooth_leds(FCRGB pfOldLeds[], FCRGB pfLeds[]){
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


unsigned long tstart,t0 ;
void loop() {
	t0=millis();
	serial_wait_frame_from_hyperion(true);
	read_leds_from_hyperion(leds);
	hyperion_read_time = millis() ;

	tstart=millis();
	color_correct(leds,fleds); 
	current_scene_sum = scene_sum(fleds);
	new_scene = detect_scene_change(current_scene_sum,old_scene_sum);

	if (new_scene) {
		if (steps_left_to_change_scene == 0) steps_left_to_change_scene = steps_to_change_scene+1;
		Serial.print("new scene!");
	}

	if (steps_left_to_change_scene > 0) { steps_left_to_change_scene -- ; }
	smooth_leds(foldleds,fleds);
	create_dither_tables(fleds);
	old_scene_sum=scene_sum(fleds);
	array_copy(fleds,foldleds,NUM_LEDS);  //<-memorizza i led attuali come led prcedenti


	step_dithering(false,00,true);
	step_dithering(false,00,true);
	step_dithering(false,00,true);
	step_dithering(false,00,true);
	Serial.print("totale: ") ; Serial.println(millis()-tstart);
	Serial.print("Grantotale: ") ; Serial.println(millis()-t0);
}

