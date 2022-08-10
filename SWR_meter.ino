#include "Arduino.h"
/*
 *
 * Inspiracje:
 * 		ATU-100 wg N7DDC
 * 			- obliczenia
 * 		SWR PWR meter wg D0ISM
 * 			- interfejs
 * założenia:
 * 	- dwa zakresy: 2500W i 650W (HYO)
 *
 *	ToDo
 *		- korekta na różne diody
 *		- co z SWRlow, SWR i int PWRround, PWRstep?
 *
 * 	testy:
 * - odczyt 10x dwóch ADC: A0 i A1:
 * 		- CyberLib: 1,34ms
 * 		- standard (analogread): 2,24ms trochę wolniej
 *
 * 	ILI9341 240x320 czcionka rozmiar: (5+1)x(7+1) (rozmiar_x + odstęp)x(rozmiar_y + odstęp); dla rozmiaru = 2 wymiary dwa razy większe
 * 			czcionka 1: 6x8
 * 			czcionka 2: 12x16
 * 			czcionka 3: 18x24
 */
#include "SWR_meter.h"

#include <Adafruit_GFX.h>	// ver. 1.2.2
#include <Adafruit_ILI9341.h>	// ver. 1.0.8
//#include <EEPROM.h>

const char tft_cs = 2;// <= /CS pin (chip-select, LOW to get attention of ILI9341, HIGH and it ignores SPI bus) default 10!
const char tft_dc = 3;// <= DC pin (1=data or 0=command indicator line) also called RS
Adafruit_ILI9341 tft = Adafruit_ILI9341(tft_cs, tft_dc);
const byte LO_HIGH_POWER_PIN = 4;		// LOW -> 650W; HIGH -> 2500W
int FWD_PIN = A0;
int REF_PIN = A1;

// z ATU:
int Power = 0, Power_old = 10000, PWR, SWR;
char work_str[9], work_str_2[9];
byte p_cnt = 0;
byte K_Mult = 33;	// ilość zwojów w transformatorze direct couplera
int SWR_old = 10000;
// D0ISM
// skala mocy:
// 5 - 2500W
// 1 - 650W
enum
{
	moc_650W = 1,
	moc_2500W = 5
};
int Scale_hi;
int y5, y8, y9;

//int PWRround, PWRstep; //переменные вид шкалы, шкала малой мощности, большой мощности, округления, шаг округления и тон

//float SWRlow, SWRhi;                                            //переменные КСВ
int a, a_old, b_old, PWRthreshold; //переменные для градусников прямой волны и КСВ
#define COLOR_SCALE ILI9341_WHITE                                                   //цвет шкалы линий КСВ и измерителя мощности

void setup()
{
#ifdef DEBUG
	Serial.begin(115200);
	Serial.println("setup...");
#endif
#ifdef CZAS_PETLI
	pinMode(CZAS_PETLI_PIN, OUTPUT);
#endif
	pinMode(LED_PIN, OUTPUT);
	pinMode(ALARM_OUT_PIN, OUTPUT);

	pinMode(LO_HIGH_POWER_PIN, INPUT_PULLUP);
	tft.begin();
	tft.setRotation(3);
	tft.fillScreen(ILI9341_BLACK);
	tft.setTextColor(ILI9341_GREEN, ILI9341_BLACK);
	tft.setTextSize(2);
	tft.setCursor(20, 90);
	tft.println("SWR & power meter v1.0.2");
	digitalWrite(LED_PIN, HIGH);
	delay(500);
	tft.setTextSize(3);
	tft.fillScreen(ILI9341_BLACK);
	y8 = 119;	// położenie potrójnej linii
	y5 = 89;	// położenie górnej skali
	y9 = 151;	// położenie dolnej skali
	show_template();
}

void loop()
{
	if (digitalRead(LO_HIGH_POWER_PIN) == LOW)
	{
		Scale_hi = moc_650W;
	}
	else
	{
		Scale_hi = moc_2500W;
	}
	skala_mocy(Scale_hi);
	get_pwr();
	pwr();
	swr();

#ifdef DEBUG
	delay(100);
#endif
#ifdef CZAS_PETLI
//#ifndef DEBUG
	//PORTD ^= (1<<PD1);		// nr portu na sztywno -> D1
	PORTB ^= (1<<PB0);		// nr portu na sztywno -> D8; aktualnie 6,09ms
//#endif
#endif
}
/*
 * wyświetlenie szablonu
 */
void show_template()
{
	//Scale_hi = EEPROM.read(3); //читаем данные с 3 ячейки памяти масштаб большой мощности
	Scale_hi = moc_2500W;
	/*
	EEPROM.get(4, SWRlow); //читаем данные с 4 по 7 ячейки памяти значение сигнализации SWRlow
	EEPROM.get(8, SWRhi); //читаем данные с 8 по 11 ячейки памяти срабатывания сигнализации SWRhi
	PWRround = EEPROM.read(12); //читаем данные с 12 ячейки памяти предела мощности округления
	PWRround = 50;
	PWRstep = EEPROM.read(13); //читаем данные с 13 ячейки памяти шага округления мощности
	PWRstep = 10;
	*/

	for (int i = 0; i < 2; i++)
	{
		// zewnętrzna podwójna ramka
		tft.drawRect(i, i, 320 - i * 2, 240 - i * 2, COLOR_DARKCYAN); //рисуем прямоугольник по внешнему контуру ЖКИ

		// linia górnej skali (SWR)
		tft.drawLine(10, y5 + i, 309, y5 + i, COLOR_SCALE); //рисуем гориз. линию верхней шкалы
		// linia dolnej skali (moc)
		tft.drawLine(10, y9 + i, 309, y9 + i, COLOR_SCALE); //рисуем гориз. линию нижней шкалы

		// górna podwójna linia
		tft.drawLine(0, 41 + i, 319, 41 + i, COLOR_DARKCYAN); //рисуем верхнюю горизонтальную линию
		// dolna podwójna linia
		tft.drawLine(0, 240-41-1+i, 319, 240-41-1+i, COLOR_DARKCYAN); //рисуем нижнюю горизонтальную линию

		// punkty skrajne górnej skali
		tft.fillRect(10 + 297 * i, y5-5, 3, 5, COLOR_SCALE); //рисуем большие риски (крайние) верхней шкалы
		// punkty skrajne dolnej skali
		tft.fillRect(10 + 297 * i, y9+1, 3, 5, COLOR_SCALE);	//рисуем большие риски (крайние) нижней шкалы
	}
	// cienkie kreski górnej skali
	for (int i = 0; i < 29; i++)
	{
		if ((i != 4) && (i != 14))                            //кроме 5-й и 15-й
			tft.fillRect(18 + i*10, y5-5, 2, 5, COLOR_SCALE);//рисуем маленькие риски верхней шкалы
	}
	// potrójna linia pomiędzy skalami
	for (int i = 0; i < 3; i++)
	{
		tft.drawLine(10, y8 + i, 309, y8 + i, COLOR_SCALE); //рисуем горизонтальные линии между шкалами
	}
	// grube kreski górnej skali (SWR)
	for (int i = 0; i < 5; i++)
	{
		tft.fillRect(57 + 50*i, y5-5, 4, 5, COLOR_SCALE); //рисуем большие риски верхней шкалы
	}
	// grube kreski dolnej skali (moc)
	for (int i = 0; i < 4; i++)
	{
		tft.fillRect(69 + 59*i, y9+1, 4, 5, COLOR_SCALE);//рисуем большие риски нижней шкалы
	}
	// cienkie kreski dolnej skali (moc)
	for (int i = 0; i < 5; i++)
	{
		tft.drawLine(41 + 59*i, y9+1, 41 + 59*i, y9+6, COLOR_SCALE);	//рисуем маленькие риски нижней шкалы
	}
	tft.setTextSize(2);
	tft.setTextColor(COLOR_SCALE, COLOR_BLACK);
	tft.setCursor(6, y5 - 22);
	tft.print("1");
	tft.setCursor(43, y5 - 22);
	tft.print("1.5");
	tft.setCursor(104, y5 - 22);
	tft.setTextColor(COLOR_YELLOW, COLOR_BLACK);
	tft.print("2");
	tft.setCursor(143, y5 - 22);
	tft.setTextColor(COLOR_ORANGE, COLOR_BLACK);
	tft.print("2.5");
	tft.setCursor(202, y5 - 22);
	tft.setTextColor(COLOR_RED, COLOR_BLACK);
	tft.print("3");
	tft.setCursor(242, y5 - 22);
	tft.print("3.5");
	tft.setCursor(302, y5 - 22);
	tft.print("4");
	skala_mocy(moc_2500W);
	tft.setTextSize(3);
	tft.setCursor(10, 12);
	tft.setTextColor(COLOR_SKYBLUE);
	tft.print("SWR");
	tft.setCursor(162, 12);
	tft.print("PWR");
}

/*
 * wyświetlenie SWR
 */
void show_swr(int swr)
{
	if (swr != SWR_old)
	{
		SWR_old = swr;
		tft.setTextSize(3);
		tft.setCursor(72, 12);
		unsigned int kolor;
		if (SWR < 200)
			kolor = ILI9341_GREEN;
		if (SWR >= 200 and SWR < 300)
			kolor = ILI9341_ORANGE;
		if (SWR >= 300)
			kolor = ILI9341_RED;
		tft.setTextColor(kolor, ILI9341_BLACK);
		if (swr == 1)
		{ // Low power

			tft.print("0.00");
			SWR_old = 0;
		}
		else
		{
			SWR_old = swr;
			itoa(swr, work_str, 10);
#ifdef DEBUG
			//if (swr > 100)
			if (true)
			{
				Serial.print("swr: ");
				Serial.println(swr);
				Serial.print("swr_str: _");
				Serial.print(work_str);
				Serial.println('_');
			}
#endif
			work_str_2[0] = work_str[0];
			work_str_2[1] = '.';
			work_str_2[2] = work_str[1];
			work_str_2[3] = work_str[2];
			tft.print(work_str_2);
		}
	}
	return;
}
/*
 * wyświetlenie mocy
 */
void show_pwr(int Power)
{
	if (Power != Power_old)
	{
		Power_old = Power;
		//
		if (Power >= 1000)
		{
			sprintf(work_str, "%4uW", Power);
		}
		else
		{
			if (Power >= 100)
			{
				sprintf(work_str, " %3uW", Power);
			}
			else if (Power >= 10)
			{
				sprintf(work_str, "  %2uW", Power);
			}
			else
			{
				sprintf(work_str, "   %1uW", Power);
			}
		}
		tft.setTextSize(3);
		tft.setCursor(224, 12);
		tft.setTextColor(COLOR_GREEN, ILI9341_BLACK);
		tft.print(work_str);
	}
}
/*
 * wyliczenie mocy i SWR
 */
void get_pwr()
{
	long Forward, Reverse;
	float p;
	//
	Forward = get_forward();
	Reverse = get_reverse();
#ifdef DEBUG
	if (Forward > 0)
	{
		Serial.print("Forward: ");
		Serial.println(Forward);
	}
	if (Reverse > 0)
	{
		Serial.print("Reverse: ");
		Serial.println(Reverse);
	}
#endif
	p = correction(Forward * 3);
#ifdef DEBUGi
    if (p > 0)
    {
    Serial.print("p: ");
    Serial.println(p);
    }
#endif

	if (Reverse >= Forward)
		Forward = 999;
	else
	{
		Forward = ((Forward + Reverse) * 100) / (Forward - Reverse);
		Serial.print("Forward2: ");
		Serial.println(Forward);

		if (Forward > 999)
			Forward = 999;
	}
	// odtąd Forward to jest wyliczony lub ustalony SWR!
	//
	p = p * K_Mult / 1000.0; // mV to Volts on Input
	p = p / 1.414;
	p = p * p / 50; // 0 - 1500 ( 1500 Watts)
	p = p + 0.5; // rounding to 0.1 W
	//
	PWR = p;
#ifdef DEBUGi
    if (PWR > 0)
    {
    Serial.print("PWR: ");
    Serial.println(PWR);
    }
#endif
	if (PWR < 5)
		SWR = 1;
	else if (Forward < 100)
		SWR = 999;
	else
		SWR = Forward;
#ifdef DEBUG
	if (PWR > 50)
	{
		Serial.print("SWR: ");
		Serial.println(SWR);
	}
#endif
	return;
}
/*
 * korekcja odczytu uwzględniająca nieliniowość diody prostującej
 */
int correction(int input)
{
	if (input <= 80)
		return 0;
	if (input <= 171)
		input += 244;
	else if (input <= 328)
		input += 254;
	else if (input <= 582)
		input += 280;
	else if (input <= 820)
		input += 297;
	else if (input <= 1100)
		input += 310;
	else if (input <= 2181)
		input += 430;
	else if (input <= 3322)
		input += 484;
	else if (input <= 4623)
		input += 530;
	else if (input <= 5862)
		input += 648;
	else if (input <= 7146)
		input += 743;
	else if (input <= 8502)
		input += 800;
	else if (input <= 10500)
		input += 840;
	else
		input += 860;
	//
	return input;
}
/*
 * odczyt poziomu fali padającej
 */
int get_forward()
{
	int forward;
	forward = analogRead(FWD_PIN);
#ifdef DEBUG
	Serial.print("forward = ");
	Serial.println(forward);
#endif
	return forward * 4.883; // zwraca napięcie w mV
}
/*
 * odczyt poziomu fali odbitej
 */
int get_reverse()
{
	int reverse;
	reverse = analogRead(REF_PIN);
	return reverse * 4.883; // zwraca napięcie w mV
}
void pwr()
{
	 int R = 0, G = 0, B = 0;
#ifdef DEBUG
		Serial.print("PWR = ");
		Serial.println(PWR);
#endif
	show_pwr(PWR);
	show_swr(SWR);
	switch (Scale_hi) {
		case moc_2500W:
			a = PWR / 41;
			break;
		case moc_650W:
			a = PWR / 11;
			break;
		default:
			break;
	}
	if (a > 60)
		a = 60;
	// linijka mocy
	if (a > a_old)
	{ //если новое значение a больше старого a_old (движение градусника вправо)
		for (int i = a_old; i < a; i++)
		{
			// czerwony:
			if (i < 30)
			{
				R = 0;
			}                      //определение красного цвета в градуснике
			if ((i >= 30) && (i <= 44))
			{
				R = map(i, 30, 44, 0, 255);
			}                      //---------------------------------------
			if (i > 44)
			{
				R = 255;
			}                      //---------------------------------------
			// zielony:
			if (i <= 14)
			{
				G = map(i, 0, 14, 0, 255);
			}                      //определение зеленого цвета в градуснике
			if ((i > 14) && (i <= 44))
			{
				G = 255;
			}                      //---------------------------------------
			if (i > 44)
			{
				G = map(i, 45, 59, 255, 0);
			}                      //---------------------------------------
			// niebieski:
			if (i <= 14)
			{
				B = 255;
			}                        //определение синего цвета в градуснике
			if (i > 29)
			{
				B = 0;
			}                        //-------------------------------------
			if ((i > 14) && (i <= 29))
			{
				B = map(i, 29, 9, 0, 255);
			}                        //-------------------------------------
			tft.fillRect(10 + i * 5, 127, 2, 20, tft.color565(R, G, B));
		}            //рисуем градиентный градусник пропорционально мощности
		a_old = a;
	}                                  //выравниваем старое и новое значение
	else if (a < a_old)
	{                                                                //иначе
		tft.fillRect(10 + a * 5, 127, 2 + (a_old - a) * 5, 20, COLOR_BLACK); //стираем градусник при движении влево
		a_old = a;
	}
}
// linijka SWR
void swr()
{
	int R = 0, G = 0, B = 0;
	int b;
	if (SWR == 1)
	{
		b = 0;
	}
	else
	{
		b = map(SWR, 100, 400, 0, 30); //расчет переменной b (кубиков в градуснике)
	}
	if (b >= 30)
	{
		b = 30;
	}                                        //ограничиваем градусник до КСВ=4.0
	if (b > b_old)
	{       //если новое значение больше старого(движение градусника вправо)
		for (int i = b_old; i < b; i++)
		{
			if (i < 15)
			{
				R = 0;
			}                      //определение красного цвета в градуснике
			// czerwony
			if ((i >= 15) && (i <= 23))
			{
				R = map(i, 15, 23, 0, 255);
			}                      //---------------------------------------
			if (i > 23)
			{
				R = 255;
			}                      //---------------------------------------
			// zielony
			if (i <= 8)
			{
				G = map(i, 0, 8, 0, 255);
			}                      //определение зеленого цвета в градуснике
			if ((i > 8) && (i <= 23))
			{
				G = 255;
			}                      //---------------------------------------
			if (i > 23)
			{
				G = map(i, 24, 29, 255, 0);
			}                      //---------------------------------------
			// niebieski
			if (i <= 8)
			{
				B = 255;
			}                        //определение синего цвета в градуснике
			if (i > 14)
			{
				B = 0;
			}                        //-------------------------------------
			if ((i > 8) && (i <= 14))
			{
				B = map(i, 14, 8, 0, 255);
			}                        //-------------------------------------
			tft.fillRect(10 + i * 10, 94, 7, 20, tft.color565(R, G, B));
		}                //рисуем градиентный градусник пропорционально КСВ
		b_old = b;
	}                                  //выравниваем старое и новое значение
	else if (b < b_old)
	{                                                                //иначе
		tft.fillRect(10 + b * 10, 94, 7 + (b_old - b) * 10, 20, COLOR_BLACK); //стираем градусник при движении влево
		b_old = b;
	}
}
void skala_mocy(int skala)
{
	switch (skala)
	{
		case moc_2500W:
			tft.setTextSize(2);
			tft.setTextColor(COLOR_SCALE, COLOR_BLACK);
			tft.setCursor(6, y9+10);
			tft.print("0");
			tft.setTextColor(COLOR_SCALE, COLOR_BLACK);
			tft.setCursor(50, y9+10);
			tft.print("500");
			tft.setTextColor(COLOR_YELLOW, COLOR_BLACK);
			tft.setCursor(106, y9+10);
			tft.print("1000");
			tft.setTextColor(COLOR_ORANGE, COLOR_BLACK);
			tft.setCursor(170, y9+10);
			tft.print("1500");
			tft.setTextColor(COLOR_RED, COLOR_BLACK);
			tft.setCursor(232, y9+10);
			tft.print("2000");
			break;
		case moc_650W:
			tft.setTextSize(2);
			tft.setTextColor(COLOR_SCALE, COLOR_BLACK);
			tft.setCursor(6, y9+10);
			tft.print("0");
			tft.setTextColor(COLOR_SCALE, COLOR_BLACK);
			tft.setCursor(50, y9+10);
			tft.print("130");
			tft.setTextColor(COLOR_YELLOW, COLOR_BLACK);
			tft.setCursor(106, y9+10);
			tft.print("260 ");
			tft.setTextColor(COLOR_ORANGE, COLOR_BLACK);
			tft.setCursor(170, y9+10);
			tft.print("390 ");
			tft.setTextColor(COLOR_RED, COLOR_BLACK);
			tft.setCursor(232, y9+10);
			tft.print("520 ");
			break;
		default:
			break;
	}
}
