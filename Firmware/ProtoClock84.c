#include <pic16f84.h>
#include <stdint.h>

static __code uint16_t __at (_CONFIG) configword1 = _CP_OFF & _WDT_OFF & _HS_OSC;

//#define CALIBRACAO 1

#define true 1
#define false 0
#define ligado 1
#define desligado 0

#define NoPush 1 
#define MaybePush 2
#define Pushed 3
#define MaybeNoPush 4

/* a=target variable, b=bit number to act upon 0-n */
#define BIT_SET(a,b) ((a) |= (1<<(b)))
#define BIT_CLEAR(a,b) ((a) &= ~(1<<(b)))
#define BIT_FLIP(a,b) ((a) ^= (1<<(b)))
#define BIT_CHECK(a,b) ((a) & (1<<(b)))

#define nop() __asm nop __endasm

#define RESET_4017() BIT_SET(PORTA, 1); nop(); nop(); BIT_CLEAR(PORTA, 1)
#define CLOCK_4017() BIT_SET(PORTA, 0); nop(); nop(); BIT_CLEAR(PORTA, 0)

#define PWM_PIN RB3
#define PONTO RA3
#define SKP RA2
#define SPK_PIN 2
#define SPK_PORT PORTA
#define SPK_PORT_DEF TRISA

void _sdcc_gsinit_startup(void)
{
  __asm pagesel _main __endasm;
  __asm goto _main __endasm;
}

const uint8_t numero[11] = { 0b10000000,
							 0b11110001,
							 0b01000100,
							 0b01100000,
							 0b00110001,
							 0b00100010,
							 0b00000010,
							 0b11110000,
							 0b00000000,
							 0b00100000,
  							 0b11110111};

uint8_t segundo;
uint8_t minuto;
uint8_t hora;
volatile uint16_t tempo;
uint8_t display;

volatile uint8_t cal;

uint8_t alarme_minuto;
uint8_t alarme_hora;
volatile uint8_t toca_alarme;
volatile uint8_t alarme_ajustando;


#define TOTAL_BOTOES 3
const uint8_t botaoIO[TOTAL_BOTOES] = {2, 4, 3};
uint8_t pushFlag[TOTAL_BOTOES];
uint8_t pushState[TOTAL_BOTOES];

#define BOTAO_MINUTO	0
#define BOTAO_HORA		1
#define BOTAO_ALARME	2

#define FLAG_BOTAO_ND		0
#define FLAG_BOTAO_SOLTO	255
#define FLAG_BOTAO_SEGURO	2000 / TEMPO_TASK1_DEF 

#define BOTAO_ALARME_PRESSIONADO (pushFlag[BOTAO_ALARME] > FLAG_BOTAO_ND && pushFlag[BOTAO_ALARME] < FLAG_BOTAO_SOLTO)
#define ALARME_INATIVO (alarme_hora == 0 && alarme_minuto == 0)

#define TEMPO_TASK1_DEF 61
#define TEMPO_TASK2_DEF 5
#define TEMPO_TASK3_DEF 201
 
volatile uint8_t tempo_task1;
volatile uint8_t tempo_task2;
volatile uint8_t tempo_task3;

void Intr(void) __interrupt
{
	//timer0 executando a cada 1mS
	if (T0IF)
	{
		TMR0 = cal;
		T0IF = 0;

		if (tempo_task1 > 0)
			tempo_task1--;

		if (tempo_task2 > 0)
			tempo_task2--;

		if (tempo_task3 > 0)
			tempo_task3--;
	
		tempo++;
		
		BIT_FLIP(PORTB, 3); //DISPLAY PWM 50% 500Hz
		
#ifdef CALIBRACAO		
		//if (tempo == 1000)
		{
			BIT_FLIP(PORTA, 3);
		}
#else
		if ((toca_alarme > 0) && PONTO)
			BIT_FLIP(SPK_PORT, SPK_PIN);
	
#endif

	}
}

void setarFlagState(uint8_t pos, uint8_t valorFlag,  uint8_t valorState)
{
	pushFlag[pos] = valorFlag;
	pushState[pos] = valorState;
}

uint8_t obterDecimal(uint8_t num)
{
	uint8_t decimal = 0;
	
	while (num>=10)
	{
		decimal++;
		num -= 10;
	}
	
	return decimal;
}

uint8_t obterUnidade(uint8_t num)
{
	while (num>=10)
	{
		num -= 10;
	}
	
	return num;
}

uint8_t obterNumeroDisplay(uint8_t num)
{
	return numero[num] | (1<<3);
}

void ligarAlarme()
{
	BIT_CLEAR(SPK_PORT_DEF, SPK_PIN);
}

void desligarAlarme()
{
	BIT_SET(SPK_PORT_DEF, SPK_PIN);
}

void task1()
{
	uint8_t ps, btn_pres, pf, btn;
	uint8_t i;
	
	if (tempo >= 1000)
	{
		tempo -= 1000;
		
		if (toca_alarme > 0)
			toca_alarme--;
		
		segundo++;
		
		//relogio atrasando
		//if((segundo % 5) == 0)
		tempo += 2;
		
		if (segundo > 59)
		{
			segundo = 0;
			minuto++;
		}
		
		if (minuto > 59)
		{
			minuto = 0;
			hora++;
		}
		
		if (hora > 23)
			hora = 0;
	}
	
	//Entrada para Botao alarme;
	BIT_SET(TRISA, 3);
	
	for (i = 0; i < TOTAL_BOTOES; i++)
	{
		ps = pushState[i];
		pf = pushFlag[i];
		btn_pres = !BIT_CHECK(PORTA, botaoIO[i]);
		switch (ps)
		{
		case NoPush: 
			if (btn_pres) 
			{
				pf = FLAG_BOTAO_ND;
				ps = MaybePush;
			}
			else 
				ps = NoPush;
			break;
		case MaybePush:
			if (btn_pres)
			{
				ps = Pushed;   
				pf = 1;
			}
			else 
				ps = NoPush;
			break;
		case Pushed:  
			if (btn_pres) 
			{
				ps = Pushed; 

				if (pf < (FLAG_BOTAO_SOLTO-1))
					pf++;
			}
			else 
				ps = MaybeNoPush;    
			break;
		case MaybeNoPush:
			if (btn_pres) 
				ps = Pushed; 
			else 
			{
				ps = NoPush;
				pf = FLAG_BOTAO_SOLTO;
			}
			break;
		}
		setarFlagState(i, pf, ps);
	}
	
	//Saida para LED de ponto;
	BIT_CLEAR(TRISA, 3);
}

void task2()
{

	PORTB = obterNumeroDisplay(10);
	CLOCK_4017();
	
	if (display == 0)
	{
		RESET_4017();
		
#ifndef CALIBRACAO		
		if (BOTAO_ALARME_PRESSIONADO)
			PORTB = obterNumeroDisplay(obterUnidade(alarme_minuto));
		else
			PORTB = obterNumeroDisplay(obterUnidade(minuto));
#else	
		PORTB = obterNumeroDisplay(obterUnidade(cal));
#endif

		display++;
	}
	else if (display == 1)
	{
#ifndef CALIBRACAO		
		if (BOTAO_ALARME_PRESSIONADO)
			PORTB = obterNumeroDisplay(obterDecimal(alarme_minuto));
		else
			PORTB = obterNumeroDisplay(obterDecimal(minuto));
#else	
			PORTB = obterNumeroDisplay(obterDecimal(cal));
#endif

		display++;
	}	
	else if (display == 2)
	{
		if (BOTAO_ALARME_PRESSIONADO)
			PORTB = obterNumeroDisplay(obterUnidade(alarme_hora));
		else
			PORTB = obterNumeroDisplay(obterUnidade(hora));
		display++;
	}
	else if (display == 3)
	{
		if (BOTAO_ALARME_PRESSIONADO)
		{
			if (obterDecimal(alarme_hora) == 0)
				PORTB = obterNumeroDisplay(10);
			else
				PORTB = obterNumeroDisplay(obterDecimal(alarme_hora));
		}
		else
		{
			if (obterDecimal(hora) == 0)
				PORTB = obterNumeroDisplay(10);
			else
				PORTB = obterNumeroDisplay(obterDecimal(hora));
		}
		display = 0;
	}

}

void task3()
{
	uint8_t pushFlagMinuto, pushFlagHora;
	
	pushFlagMinuto = pushFlag[BOTAO_MINUTO];
	pushFlagHora = pushFlag[BOTAO_HORA];
	
	if (pushFlagMinuto > FLAG_BOTAO_SEGURO)
	{
		if (pushFlagMinuto == FLAG_BOTAO_SOLTO)
			pushFlag[BOTAO_MINUTO] = FLAG_BOTAO_ND;
		
		if (toca_alarme > 0)
		{
			toca_alarme = 0;
		}
		else
		{
			if (BOTAO_ALARME_PRESSIONADO)
			{
				alarme_minuto++;		
				
				if (alarme_minuto > 59)
					alarme_minuto = 0;
				
				alarme_ajustando = 1;
			}
			else 
			{
				segundo = 0;
				minuto++;		

#ifdef CALIBRACAO				
				cal++;
#endif
				
				if (minuto > 59)
					minuto = 0;
			}
		}
		
	}
	else if (pushFlagHora > FLAG_BOTAO_SEGURO)
	{
		if (pushFlagHora == FLAG_BOTAO_SOLTO)
			pushFlag[BOTAO_HORA] = FLAG_BOTAO_ND;

		if (toca_alarme > 0)
		{
			toca_alarme = 0;	
		}
		else
		{
			if (BOTAO_ALARME_PRESSIONADO)
			{
				alarme_hora++;		
				
				if (alarme_hora > 23)
					alarme_hora = 0;
			}
			else
			{
				hora++;		

#ifdef CALIBRACAO
				cal--;
#endif

				if (hora > 23)
					hora = 0;
			}
		}

	}
}

void main()
{
	uint8_t i;
	cal = 10; //medido no osciloscopio. aprox 501Hz
	
	TRISB = 0;
	TRISA = 0b00010100;

	PORTB = 0;
	PORTA = 0;
  
	//timer0 configurado 1ms
	OPTION_REG &= 0xC0 | 0x01;
	TMR0 = cal;
	T0IE = 1;
	GIE = 1;

	RB3 = 1;
  
	if (segundo > 59)
		segundo = 0;
	
	if (minuto > 59)
		minuto = 0;
	
	if (hora > 23)
		hora = 0;
	
	tempo = 0;

	alarme_minuto = 0;
	alarme_hora = 0;
	toca_alarme = 0;
  
	tempo_task1 = TEMPO_TASK1_DEF;
	tempo_task2 = TEMPO_TASK2_DEF;
	tempo_task3 = TEMPO_TASK3_DEF;

	display = 0;

	for (i = 0; i < TOTAL_BOTOES; i++)
	{
		setarFlagState(i, FLAG_BOTAO_ND, NoPush);
	}
	
  for (;;) 
  {
    if (tempo_task1 == 0)
	{
		tempo_task1 = TEMPO_TASK1_DEF;
		task1();
	}
	
	if (tempo_task2 == 0)
	{
		tempo_task2 = TEMPO_TASK2_DEF;
		task2();
	}

	if (tempo_task3 == 0)
	{
		tempo_task3 = TEMPO_TASK3_DEF;
		task3();
	}
	
	if (!ALARME_INATIVO)
	{
		if ((hora == alarme_hora) && (minuto == alarme_minuto) && (segundo == 0))
		{
			toca_alarme = 90;
			ligarAlarme();
		}
		
		if (toca_alarme == 0)
			desligarAlarme();
	}
	
#ifndef CALIBRACAO	
	PONTO = (tempo < 500) ? 0 : 1;
#endif
	
	
  }
  
}
