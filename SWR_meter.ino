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
 *		- skala mocy 2500: co 500 pięć odcinków
 *		- skala mocy 650: co 100 sześć odcinków?
 *		- skala SWR do 5? (ile się zmieści)
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

#include <Adafruit_GFX.h>	// ver. 1.11.3
#include <Adafruit_ILI9341.h>	// ver. 1.2.0
//#include <Fonts/FreeSansBoldOblique9pt7b.h>
#include <EEPROM.h>

const char tft_cs = 2;// <= /CS pin (chip-select, LOW to get attention of ILI9341, HIGH and it ignores SPI bus) default 10!
const char tft_dc = 3;// <= DC pin (1=data or 0=command indicator line) also called RS
Adafruit_ILI9341 tft = Adafruit_ILI9341(tft_cs, tft_dc);
//Bounce2::Button lo_high_power = Bounce2::Button();
const byte LO_HIGH_POWER_PIN = 4;
int FWD_PIN = A0;
int REF_PIN = A1;

int updateIndex = 0;
// z ATU:
int Power = 0, Power_old = 10000, PWR, SWR;
char work_str[9], work_str_2[9];
byte p_cnt = 0;
byte K_Mult = 33;	// ilość zwojów w transformatorze direct couplera
int SWR_old = 10000;
// D0ISM
int View_scale; 	// rodzaj linijki: 0 - z podłożem; 1 bez podłoża (tła)
int Scale_low, Scale_hi = 5, PWRround, PWRstep; //переменные вид шкалы, шкала малой мощности, большой мощности, округления, шаг округления и тон

float SWRlow, SWRhi;                                            //переменные КСВ
int a, a_old, b_old, PWRthreshold; //переменные для градусников прямой волны и КСВ
float V, t; //переменные для изм. мощности, КСВ, напряжения, температуры
#define COLOR_SCALE ILI9341_WHITE                                                   //цвет шкалы линий КСВ и измерителя мощности
int flag_power = 1;              //флаг переключения низкой или высокой мощности
int flag_scale = 1;                  //переменная флага состояния шкалы мощности

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
	pinMode(LO_HIGH_POWER_PIN, INPUT_PULLUP);		// stan aktywny niski?
	/*
	 lo_high_power.attach(LO_HIGH_POWER_PIN, INPUT_PULLUP);
	 lo_high_power.setPressedState(LOW);
	 lo_high_power.interval(5);
	 */
	tft.begin();
	tft.setRotation(3);
	tft.fillScreen(ILI9341_BLACK);
	tft.setTextColor(ILI9341_GREEN, ILI9341_BLACK);
	//tft.setFont(&FreeSansBoldOblique9pt7b);                                    //используем внешний шрифт FreeSansBoldOblique9pt7b

	tft.setTextSize(2);
	tft.setCursor(20, 90);
	tft.println("SWR & power meter v.1.0");
	digitalWrite(LED_PIN, HIGH);
	delay(500);
	tft.setTextSize(3);
	tft.fillScreen(ILI9341_BLACK);
	show_template();
}

void loop()
{
	if (digitalRead(LO_HIGH_POWER_PIN) == LOW)
	{

	}

	pwr();

	//lo_high_power.update();
#ifdef DEBUG
	delay(400);
#endif
#ifdef CZAS_PETLI
//#ifndef DEBUG
	//PORTD ^= (1<<PD1);		// nr portu na sztywno -> D1
	PORTB ^= (1<<PB0);		// nr portu na sztywno -> D8; aktualnie 6,09ms
//#endif
#endif
}
void show_template()
{
	View_scale = EEPROM.read(1);    //читаем данные с 1 ячейки памяти вида шкалы
	Scale_low = EEPROM.read(2); //читаем данные с 2 ячейки памяти масштаб малой мощности
	Scale_hi = EEPROM.read(3); //читаем данные с 3 ячейки памяти масштаб большой мощности
	Scale_hi = 5;
	EEPROM.get(4, SWRlow); //читаем данные с 4 по 7 ячейки памяти значение сигнализации SWRlow
	EEPROM.get(8, SWRhi); //читаем данные с 8 по 11 ячейки памяти срабатывания сигнализации SWRhi
	PWRround = EEPROM.read(12); //читаем данные с 12 ячейки памяти предела мощности округления
	PWRround = 50;
	PWRstep = EEPROM.read(13); //читаем данные с 13 ячейки памяти шага округления мощности
	PWRstep = 10;
	//Tone=      EEPROM.read(14);                                                   //читаем данные с 14 ячейки памяти значение тональности сигнала
	//коэф. мощности и пороги переключения для малой и большой мощности

	for (int i = 0; i < 2; i++)
	{
		tft.drawRect(i, i, 320 - i * 2, 240 - i * 2, COLOR_DARKCYAN); //рисуем прямоугольник по внешнему контуру ЖКИ
		tft.drawLine(10, 66 + i, 309, 66 + i, COLOR_SCALE); //рисуем гориз. линию верхней шкалы
		tft.drawLine(10, 111 + i, 309, 111 + i, COLOR_SCALE); //рисуем гориз. линию нижней шкалы
		tft.drawLine(0, 35 + i, 319, 35 + i, COLOR_DARKCYAN); //рисуем верхнюю горизонтальную линию
		tft.drawLine(0, 164 + i, 319, 164 + i, COLOR_DARKCYAN); //рисуем нижнюю горизонтальную линию

		tft.fillRect(10 + 297 * i, 61, 3, 5, COLOR_SCALE); //рисуем большие риски (крайние) верхней шкалы
		tft.fillRect(10 + 297 * i, 113, 3, 5, COLOR_SCALE);	//рисуем большие риски (крайние) нижней шкалы
	}

	for (int i = 0; i < 29; i++)
	{
		if ((i != 4) && (i != 14))                            //кроме 5-й и 15-й
			tft.fillRect(18 + i * 10, 63, 2, 4, COLOR_SCALE);//рисуем маленькие риски верхней шкалы
	}
	// potrójna linia pomiędzy skalami
	for (int i = 0; i < 3; i++)
	{
		tft.drawLine(10, 88 + i, 309, 88 + i, COLOR_SCALE); //рисуем горизонтальные линии между шкалами
	}
	for (int i = 0; i < 5; i++)
	{
		tft.fillRect(57 + 50 * i, 61, 4, 5, COLOR_SCALE); //рисуем большие риски верхней шкалы
	}
	// grube kreski dolnej skali
	for (int i = 0; i < 4; i++)
	{
		tft.fillRect(65 + 63 * i, 113, 4, 5, COLOR_SCALE);//рисуем большие риски нижней шкалы
	}

	for (int i = 0; i < 4; i++)
	{
		tft.drawLine(31 + 50 * i, 113, 31 + 50 * i, 117, COLOR_SCALE);//рисуем маленькие риски нижней шкалы
	}
	tft.setTextSize(2);
	tft.setTextColor(COLOR_SCALE, COLOR_BLACK);
	tft.setCursor(6, 45);
	tft.print("1");
	tft.setCursor(43, 45);
	tft.print("1.5");
	tft.setCursor(104, 45);
	tft.setTextColor(COLOR_YELLOW, COLOR_BLACK);
	tft.print("2");
	tft.setCursor(143, 45);
	tft.setTextColor(COLOR_ORANGE, COLOR_BLACK);
	tft.print("2.5");
	tft.setCursor(202, 45);
	tft.setTextColor(COLOR_RED, COLOR_BLACK);
	tft.print("3");
	tft.setCursor(242, 45);
	tft.print("3.5");
	tft.setCursor(302, 45);
	tft.print("4");
	if (Scale_hi == 5)
	{                                    //если выбрана выходная шкала 4
		//tft.drawGFXText(6, 135, "0", COLOR_SCALE);   //выводим текст "0"
		tft.setTextSize(2);
		tft.setTextColor(COLOR_SCALE, COLOR_BLACK);
		tft.setCursor(6, 135);
		tft.print("0");
		//tft.drawGFXText(38, 135, "250", COLOR_SCALE); //выводим текст "250"
		tft.setTextColor(COLOR_SCALE, COLOR_BLACK);
		tft.setCursor(50, 135);
		tft.print("500");
		//tft.drawGFXText(88, 135, "500", COLOR_YELLOW); 		//выводим текст "500"
		tft.setTextColor(COLOR_YELLOW, COLOR_BLACK);
		tft.setCursor(106, 135);
		tft.print("1000");
		//tft.drawGFXText(138, 135, "750", COLOR_ORANGE); 	//выводим текст "750"
		tft.setTextColor(COLOR_ORANGE, COLOR_BLACK);
		tft.setCursor(170, 135);
		tft.print("1500");
		//tft.drawGFXText(188, 135, "1k", COLOR_RED);			//выводим текст "1k"
		tft.setTextColor(COLOR_RED, COLOR_BLACK);
		tft.setCursor(232, 135);
		tft.print("2000");
	}
	tft.setTextSize(3);
	tft.setCursor(10, 8);
	tft.setTextColor(COLOR_SKYBLUE);
	tft.print("SWR");
	tft.setCursor(152, 8);
	tft.print("PWR");
}

/*
 * wyliczenie i wyświetlenie mocy i SWR
 */
void lcd_pwr()
{
	get_pwr();
	//lcd_swr(SWR);
	//show_pwr(PWR);
	return;
}
/*
 * wyświetlenie SWR
 */
void lcd_swr(int swr)
{
	if (swr != SWR_old)
	{
		SWR_old = swr;
		tft.setTextSize(4);
		tft.setCursor(52, 134);
		tft.setTextColor(ILI9341_WHITE, ILI9341_BLACK);
		tft.print("SWR ");
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
		tft.setCursor(224, 8);
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
int get_reverse()
{
	int reverse;
	reverse = analogRead(REF_PIN);
	return reverse * 4.883; // zwraca napięcie w mV
}
void pwr()
{
	 int R, G, B;

	//переменные цветов
	if (flag_power == 1)
	{                                        //проверяем флаг состояния мощности
		/*
		if (flag_scale == 1)
		{
			flag_scale++; //проверяем флаг состояния шкалы, увеличиваем его на 1
			//tft.fillRectangle(5, 122, 215, 136, COLOR_BLACK); //стираем старую шкалу черным цветом
			tft.fillRect(5, 122, 215 - 5, 136 - 122, COLOR_BLACK); //стираем старую шкалу черным цветом

		}
		*/
		get_pwr();
		/*
		 if (PWR >= PWRround * 10)
		 {
		 int PWR1 = PWR / PWRstep;
		 PWR = PWR1 * PWRstep;//свыше PWRround показания мощности приводим кратно PWRstep
		 }
		 */
#ifdef DEBUG
		Serial.print("PWR = ");
		Serial.println(PWR);
#endif
		show_pwr(PWR);
		/*
		 dtostrf(PWR, len, 0, buf);       //преобразуем значение PWR в массив buf
		 buf[len] = 'W';
		 buf[len + 1] = 0;                           //добавляем к массиву знак W
		 //tft.drawText(153, 8, buf + String(' '), COLOR_GREEN);//выводим значение мощности
		 tft.setCursor(153, 8);
		 tft.setTextColor(COLOR_GREEN);
		 tft.print(buf);
		 */
	}
	if (flag_power == 1)
	{         //выбираем масштаб градусника выходной мощности, если флаг равен 1
		if (Scale_hi == 0)
		{
			a = PWR / 5;
		}                                                      //для 200Вт
		if (Scale_hi == 1)
		{
			a = PWR / 10;
		}                                                     //для 400Вт
		if (Scale_hi == 2)
		{
			a = PWR / 15;
		}                                                     //для 600Вт
		if (Scale_hi == 3)
		{
			a = PWR / 20;
		}                                                     //для 800Вт
		if (Scale_hi == 4)										//для 1000Вт
		{
			a = PWR / 25;
		}
		if (Scale_hi == 5)
		{
			a = PWR / 42;
		}
	}
	if (View_scale == 1)
	{                                       //если выбран вид шкалы без подложки
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
				//tft.fillRectangle(10 + i*5, 93, 12 + i*5, 108, tft.setColor(R, G, B));
				tft.fillRect(10 + i*5, 93, 2, 15, tft.color565(R, G, B)) ;
			}            //рисуем градиентный градусник пропорционально мощности
			a_old = a;
		}                                  //выравниваем старое и новое значение
		else
		{                                                                //иначе
			//tft.fillRectangle(10 + a * 5, 93, 12 + a_old * 5, 108, COLOR_BLACK); //стираем градусник при движении влево
			tft.fillRect(10 + a*5, 93, 2 + (a_old - a)*5, 15, COLOR_BLACK); //стираем градусник при движении влево
			a_old = a;
		}
	}                                      //выравниваем старое и новое значение
}
