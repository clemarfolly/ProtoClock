/*
 * ProtoClock - Firmware para PIC16F84
 *
 * Relogio digital com 4 displays de 7 segmentos + ponto decimal.
 * Alarme configuravel.
 * Calibracao de timer via osciloscopio.
 *
 * Hardware (conforme esquematico ProtoClock.sch):
 *   PIC16F84, cristal 4MHz (Y1)
 *   4017 (U3) - decodificador para selecao de display
 *   74HC08/74LS08 (U2A-E) - AND gates para PWM de brilho via RB3
 *   BC558 (Q1) - transistor PNP para buzzer via RA2
 *   2x LEDs (D1, D2) - ponto decimal, compartilhados com botao alarme em RA3
 *
 * Pinagem:
 *   RA0 = 4017 CLK (clock do decodificador)
 *   RA1 = 4017 RESET (reseta para Q0)
 *   RA2 = Speaker (base BC558 via R4 1k) + Botao MIN (SW2, R2 10k pull-up)
 *   RA3 = LED Ponto Decimal (D1/D2 via R5 220R) + Botao ALARME (SW3, R3 10k pull-up)
 *   RA4 = Botao HORA (SW1, R1 10k pull-up)
 *   RB0-RB7 = Segmentos display + RB3 = PWM brilho (74HC08 pin 13)
 *
 * Botoes (ativo baixo, pull-up externo 10k para +5V):
 *   MIN    = RA2  (compartilhado com speaker - LIMITACAO: nao funciona durante alarme)
 *   HORA   = RA4  (dedicado)
 *   ALARME = RA3  (compartilhado com LED ponto)
 *
 * Display:
 *   4x LTS-6960HR (catodo comum)
 *   Selecao via 4017 (Q0-Q3) + 74HC08 AND gates
 *   Brilho via PWM em RB3 (500Hz, ~50% duty cycle)
 *
 * Compilar com SDCC 4.3.0 para PIC16F84.
 * Usar gpasm/gplink (gputils) para montagem e linkagem.
 *
 * Notas:
 *   - O crystal de 4MHz nao e preciso. O valor de cal foi obtido
 *     com osciloscopio para ajuste fino do Timer0 (~1ms).
 *   - Botao MIN (RA2) compartilha pino com speaker. Enquanto o
 *     alarme toca, o speaker alterna RA2, impedindo leitura do botao.
 *     Apenas HORA e ALARME podem desligar o alarme.
 *   - Botao ALARME (RA3) compartilha pino com LED ponto.
 *     O firmware alterna RA3 entre saida (LED) e entrada (botao)
 *     ciclicamente para evitar conflito.
 */

#include <pic16f84.h>
#include <stdint.h>

static __code uint16_t __at (_CONFIG) configword1 = _CP_OFF & _WDT_OFF & _HS_OSC;

//#define CALIBRACAO 1

#define true 1
#define false 0
#define ligado 1
#define desligado 0

/* Estados da maquina de debounce dos botoes */
#define NoPush      1   /* Botao solto, sem atividade */
#define MaybePush   2   /* Possivel pressionamento (aguarda confirmacao) */
#define Pushed      3   /* Botao confirmado pressionado */
#define MaybeNoPush 4   /* Possivel soltura (aguarda confirmacao) */

/* Macros de manipulacao de bits */
#define BIT_SET(a,b)   ((a) |= (1<<(b)))
#define BIT_CLEAR(a,b) ((a) &= ~(1<<(b)))
#define BIT_FLIP(a,b)  ((a) ^= (1<<(b)))
#define BIT_CHECK(a,b) ((a) & (1<<(b)))

#define nop() __asm nop __endasm

/*
 * Macros protegidas com do/while(0).
 * Evita bug se forem usadas dentro de if/else.
 */
#define RESET_4017() \
    do { \
        BIT_SET(PORTA, 1); \
        nop(); \
        nop(); \
        BIT_CLEAR(PORTA, 1); \
    } while (0)

#define CLOCK_4017() \
    do { \
        BIT_SET(PORTA, 0); \
        nop(); \
        nop(); \
        BIT_CLEAR(PORTA, 0); \
    } while (0)

/* PWM do display via RB3 (74HC08 AND gate) */
#define PWM_PIN RB3

/* Ponto decimal (LEDs D1/D2) - compartilhado com botao ALARME */
#define PONTO RA3

/* Speaker (buzzer via BC558 PNP) - compartilhado com botao MIN */
#define SPK_PIN 2
#define SPK_PORT PORTA
#define SPK_PORT_DEF TRISA

/*
 * SPK_IDLE_HIGH = 1: transistor PNP, nivel alto desliga.
 * RA2 em nivel alto  -> base alta  -> PNP corte  -> buzzer off.
 * RA2 em nivel baixo -> base baixa -> PNP conduz -> buzzer on.
 */
#define SPK_IDLE_HIGH 1

/*
 * SPK_COMPARTILHADO_COM_BOTAO = 1: RA2 e usado tanto para speaker
 * quanto para botao MIN. Enquanto o alarme toca, o botao MIN e
 * ignorado porque o speaker esta alternando o pino.
 *
 * LIMITACAO DE HARDWARE: O botao MIN (RA2) nao pode desligar o
 * alarme enquanto ele toca. Apenas HORA (RA4) e ALARME (RA3)
 * podem desligar.
 */
#define SPK_COMPARTILHADO_COM_BOTAO 1

/*
 * Tempo maximo do alarme em segundos.
 * Define por quanto tempo o alarme toca automaticamente.
 * Se 0, toca ate ser desligado manualmente.
 * Se 255 (~4 minutos), toca por bastante tempo.
 * Valor recomendado: 90 (1 minuto e 30 segundos).
 */
#define TEMPO_ALARM_SEG 90

void _sdcc_gsinit_startup(void)
{
    __asm pagesel _main __endasm;
    __asm goto _main __endasm;
}

/*
 * Tabela de segmentos para display de 7 segmentos + ponto.
 * Indice 0-9: digitos 0-9.
 * Bit 7 (MSB) = segmento 'a' (MSB do PORTB).
 *
 * Configuracao do display LTS-6960HR (catodo comum):
 *   RB0=a, RB1=b, RB2=c, RB3=d, RB4=e, RB5=f, RB6=g, RB7=DP
 *
 * Para acender um segmento, o bit correspondente deve ser 0 (ativo baixo).
 * Para apagar, bit = 1.
 *
 * Exemplo: numero[0] = 0b10000000 -> apenas DP apagado (todos ligados = "0")
 *          numero[1] = 0b11110001 -> apenas b,c ligados = "1"
 */
const uint8_t numero[10] = {
    0b10000000,  /* 0: a,b,c,d,e,f ligados */
    0b11110001,  /* 1: b,c ligados */
    0b01000100,  /* 2: a,b,d,e,g ligados */
    0b01100000,  /* 3: a,b,c,d,g ligados */
    0b00110001,  /* 4: b,c,f,g ligados */
    0b00100010,  /* 5: a,c,d,f,g ligados */
    0b00000010,  /* 6: a,c,d,e,f,g ligados */
    0b11110000,  /* 7: a,b,c ligados */
    0b00000000,  /* 8: todos ligados */
    0b00100000   /* 9: a,b,c,d,f,g ligados */
};

/* Variaveis de tempo */
uint8_t segundo;
uint8_t minuto;
uint8_t hora;

/*
 * tempo: contador de milissegundos na ISR.
 * Nao e mais lido diretamente por task1/main.
 */
static volatile uint16_t tempo;

/*
 * ponto_sw: estado software do ponto decimal.
 * 0.5s ligado / 0.5s desligado.
 * Substitui leitura direta de PONTO/RA3 na ISR,
 * evitando conflito com botao de alarme em RA3.
 */
volatile uint8_t ponto_sw;

/*
 * tick_segundo: incrementado pela ISR a cada 1s.
 * task1 consome esse valor para contar segundos.
 * Deve ser lido com GIE=0 para atomicidade (embora uint8_t
 * seja atomico no PIC16F84, e boa pratica).
 */
volatile uint8_t tick_segundo;

/* Display e calibracao */
uint8_t display;
volatile uint8_t cal;

/* Alarme */
uint8_t alarme_minuto;
uint8_t alarme_hora;

/*
 * toca_alarme: contador regressivo de segundos do alarme.
 * Decrementado por task1 a cada segundo.
 * 0 = alarme desligado, >0 = alarme tocando.
 * Quando chega a 0, alarme para automaticamente.
 */
volatile uint8_t toca_alarme;

/*
 * alarme_ajustando: 1 quando o usuario esta ajustando o alarme.
 * Impede disparo enquanto ajusta.
 * Limpo quando botao ALARME e solto.
 */
volatile uint8_t alarme_ajustando;

/*
 * alarme_bloqueado: 1 apos alarme disparar ou ser descartado manualmente.
 * Impede re-disparo no mesmo minuto.
 * E limpo pelo loop principal quando o horario nao mais coincide
 * com o horario do alarme (ou seja, quando o minuto avanca).
 */
volatile uint8_t alarme_bloqueado;

/* Botoes */
#define TOTAL_BOTOES 3

/*
 * Mapeamento dos botoes para pinos do PIC.
 * botaoIO[0] = RA2 = MIN  (compartilhado com speaker)
 * botaoIO[1] = RA4 = HORA (dedicado)
 * botaoIO[2] = RA3 = ALARME (compartilhado com LED ponto)
 */
const uint8_t botaoIO[TOTAL_BOTOES] = {2, 4, 3};

uint8_t pushFlag[TOTAL_BOTOES];
uint8_t pushState[TOTAL_BOTOES];

#define BOTAO_MINUTO  0
#define BOTAO_HORA    1
#define BOTAO_ALARME  2

/* Tempos das tasks (em milissegundos) */
#define TEMPO_TASK1_DEF 61   /* leitura de botoes + relogio (~16Hz) */
#define TEMPO_TASK2_DEF 5    /* multiplexacao do display (~200Hz) */
#define TEMPO_TASK3_DEF 201  /* acoes dos botoes (~5Hz) */

/* Limites do debounce */
#define FLAG_BOTAO_ND     0
#define FLAG_BOTAO_SOLTO  255
#define FLAG_BOTAO_SEGURO (2000 / TEMPO_TASK1_DEF)  /* ~33 = ~2s */

/*
 * Macro: botao ALARME esta pressionado?
 * Verifica se pushFlag esta entre ND e SOLTO.
 * ND=0 (nunca pressionado) e SOLTO=255 (liberado).
 * Valores entre 1 e 254 indicam que o botao esta ou esteve pressionado.
 */
#define BOTAO_ALARME_PRESSIONADO \
    ((pushFlag[BOTAO_ALARME] > FLAG_BOTAO_ND) && \
     (pushFlag[BOTAO_ALARME] < FLAG_BOTAO_SOLTO))

/*
 * Macro: alarme esta inativo (desabilitado)?
 * Quando hora=0 e minuto=0, o alarme esta desabilitado.
 */
#define ALARME_INATIVO \
    ((alarme_hora == 0) && (alarme_minuto == 0))

/*
 * Macro: verifica se o indice i corresponde ao botao
 * compartilhado com o speaker (RA2 = MIN).
 * Retorna 1 se i==0 (BOTAO_MINUTO), 0 caso contrario.
 */
#define SPK_BOTAO_COMPARTILHADO(i) ((i) == 0)

/* Timers das tasks (contagem regressiva, zerados pela ISR) */
volatile uint8_t tempo_task1;
volatile uint8_t tempo_task2;
volatile uint8_t tempo_task3;

/*
 * Rotina de interrupcao.
 * Timer0 configurado para ~1ms (prescaler 1:4, 4MHz, cal=10).
 *
 * Responsabilidades:
 *   1. Decrementar contadores das tasks
 *   2. Contar milissegundos em 'tempo'
 *   3. Detectar transicao de 1 segundo (tick_segundo)
 *   4. Atualizar ponto_sw (0.5s on/off)
 *   5. PWM do display (RB3 toggle a cada 1ms)
 *   6. Alternar speaker quando alarme toca (beep intermitente)
 */
void Intr(void) __interrupt
{
    if (T0IF)
    {
        TMR0 = cal;
        T0IF = 0;

        /* Decrementa contadores das tasks */
        if (tempo_task1 > 0)
            tempo_task1--;

        if (tempo_task2 > 0)
            tempo_task2--;

        if (tempo_task3 > 0)
            tempo_task3--;

        tempo++;

        /* Deteccao de 1 segundo */
        if (tempo >= 1000)
        {
            tempo -= 1000;

            /*
             * Compensacao de atraso do loop principal.
             * Ajustar conforme calibragem no osciloscopio.
             */
            tempo += 2;

            tick_segundo++;
        }

        /* Ponto decimal: 0.5s ligado, 0.5s desligado (1Hz) */
        ponto_sw = (tempo >= 500);

        /*
         * PWM do display via RB3 (74HC08 AND gate).
         * Alterna a cada 1ms = 500Hz, ~50% duty cycle.
         * Controla o brilho dos displays.
         */
        BIT_FLIP(PORTB, 3);

#ifdef CALIBRACAO
        /*
         * Modo calibracao: alterna RA3 para medir frequencia
         * no osciloscopio. Usado para ajustar 'cal'.
         */
        BIT_FLIP(PORTA, 3);
#else
        /*
         * Speaker: alterna RA2 quando alarme toca e ponto esta ligado.
         * Usa ponto_sw (variavel software) em vez de PONTO (RA3)
         * para evitar conflito com botao de alarme em RA3.
         *
         * Frequencia do beep: ~500Hz (1ms toggle = 2ms periodo).
         * O ponto_sw gera beep intermitente (0.5s on, 0.5s off).
         */
        if ((toca_alarme > 0) && ponto_sw)
            BIT_FLIP(SPK_PORT, SPK_PIN);
#endif
    }
}

/*
 * Obtem o digito decimal (dezena) de um numero 0-99.
 * Usa subtracao em loop para evitar divisao (cara em PIC14).
 */
uint8_t obterDecimal(uint8_t num)
{
    uint8_t decimal = 0;

    while (num >= 10)
    {
        decimal++;
        num -= 10;
    }

    return decimal;
}

/*
 * Obtem a unidade de um numero 0-99.
 */
uint8_t obterUnidade(uint8_t num)
{
    while (num >= 10)
    {
        num -= 10;
    }

    return num;
}

/*
 * Converte digito (0-9) para formato do display.
 * Adiciona bit 3 (ponto decimal ligado = segmento apagado).
 */
uint8_t obterNumeroDisplay(uint8_t num)
{
    return numero[num] | (1 << 3);
}

/*
 * Task1: leitura de botoes + contagem de tempo.
 * Executa a cada ~61ms (TEMPO_TASK1_DEF).
 *
 * Responsabilidades:
 *   1. Consumir ticks de segundo da ISR
 *   2. Atualizar segundo/minuto/hora
 *   3. Ler botoes com debounce (maquina de estados)
 *   4. Desligar alarme quando botao e pressionado
 *   5. Controlar LED ponto (RA3)
 */
void task1(void)
{
    uint8_t ps;
    uint8_t btn_pres;
    uint8_t pf;
    uint8_t i;
    uint8_t ticks;

    /* Consome ticks de forma atomica */
    GIE = 0;
    ticks = tick_segundo;
    tick_segundo = 0;
    GIE = 1;

    /* Processa ticks acumulados (normalmente 1, pode ser mais se task1 atrasou) */
    while (ticks > 0)
    {
        ticks--;

        segundo++;

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

    /*
     * Preparacao para leitura do botao ALARME em RA3.
     *
     * RA3 e compartilhado com LED de ponto.
     * Antes de ler como entrada, forca saida baixa para evitar
     * conflito com botao ligado ao GND via R3 (10k).
     */
    PONTO = 0;
    BIT_SET(TRISA, 3);  /* RA3 = entrada para leitura do botao */

    /* Leitura dos botoes com debounce (maquina de estados) */
    for (i = 0; i < TOTAL_BOTOES; i++)
    {
#if SPK_COMPARTILHADO_COM_BOTAO
        /*
         * Se o pino do speaker esta sendo usado como botao,
         * nao le enquanto o alarme toca.
         * Isso evita leituras falsas causadas pela alternancia do buzzer.
         *
         * LIMITACAO: Botao MIN (RA2) nao funciona durante alarme.
         */
        if (SPK_BOTAO_COMPARTILHADO(i) && (toca_alarme > 0))
        {
            pushFlag[i] = 0;
            pushState[i] = 1;  /* MaybePush - mantem estado seguro */
            continue;
        }
#endif

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
                {
                    ps = NoPush;
                }
                break;

            case MaybePush:
                if (btn_pres)
                {
                    ps = Pushed;
                    pf = 1;  /* Inicia contador de duracao */
                }
                else
                {
                    ps = NoPush;  /* Falso positivo - ignorar */
                }
                break;

            case Pushed:
                if (btn_pres)
                {
                    ps = Pushed;

                    /* Incrementa duracao (maximo ate FLAG_BOTAO_SOLTO-1) */
                    if (pf < (FLAG_BOTAO_SOLTO - 1))
                        pf++;
                }
                else
                {
                    ps = MaybeNoPush;  /* Possivel soltura - aguarda confirmacao */
                }
                break;

            case MaybeNoPush:
                if (btn_pres)
                {
                    ps = Pushed;  /* Falso negativo - volta para Pushed */
                }
                else
                {
                    ps = NoPush;
                    pf = FLAG_BOTAO_SOLTO;  /* Confirmado: botao foi liberado */
                }
                break;

            default:
                ps = NoPush;
                pf = FLAG_BOTAO_ND;
                break;
        }

        pushFlag[i] = pf;
        pushState[i] = ps;
    }

    /* Volta RA3 como saida (controle do LED ponto) */
    BIT_CLEAR(TRISA, 3);

    /*
     * Desligar alarme: qualquer botao pressionado interrompe.
     *
     * Devido ao compartilhamento RA2/speaker, o botao MIN e
     * ignorado enquanto o alarme toca (ja filtrado acima).
     * Apenas HORA (RA4) e ALARME (RA3) podem desligar o alarme.
     */
    if (toca_alarme > 0)
    {
        for (i = 0; i < TOTAL_BOTOES; i++)
        {
#if SPK_COMPARTILHADO_COM_BOTAO
            if (SPK_BOTAO_COMPARTILHADO(i) && (toca_alarme > 0))
                continue;
#endif

            if (pushState[i] == Pushed)
            {
                toca_alarme = 0;
                alarme_bloqueado = 1;
                break;
            }
        }
    }

    /*
     * Limpar alarme_ajustando quando botao ALARME nao esta pressionado.
     *
     * Verifica pushState (nao apenas pushFlag) para evitar limpar
     * durante janela MaybePush (~61ms) onde pushFlag ainda e 0
     * mas o botao ja esta sendo pressionado.
     */
    if (pushState[BOTAO_ALARME] == NoPush)
        alarme_ajustando = 0;

    /*
     * Controle do LED ponto (RA3).
     *
     * Enquanto o botao ALARME estiver sendo pressionado (qualquer estado
     * alem de NoPush), mantem PONTO em 0 para evitar conflito de saida.
     *
     * A janela MaybePush (~61ms) pode ocorrer entre a deteccao do
     * pressionamento e a confirmacao. Durante esse periodo, o botao
     * pode estar puxando RA3 para baixo. Manter PONTO=0 evita
     * conflito de saida (driver tentando ir alto enquanto botao puxa baixo).
     */
    if (pushState[BOTAO_ALARME] != NoPush)
        PONTO = 0;
    else
        PONTO = ponto_sw;
}

/*
 * Task2: multiplexacao do display.
 * Executa a cada ~5ms (TEMPO_TASK2_DEF).
 *
 * Cicla entre os 4 displays usando o 4017.
 * O 4017 e resetado no display 0 e avancado a cada chamada.
 * RB3 (PWM) controla o brilho via 74HC08 AND gate.
 *
 * Quando botao ALARME esta pressionado, mostra o horario do alarme.
 * Caso contrario, mostra o horario atual.
 *
 * Ordem dos displays (esquerda para direita):
 *   display 0 = unidade do minuto (aff1)
 *   display 1 = dezena do minuto (aff2)
 *   display 2 = unidade da hora (aff3)
 *   display 3 = dezena da hora (aff4)
 */
void task2(void)
{
    PORTB = 0xFF;  /* Apaga todos os segmentos (blanking) */
    CLOCK_4017();  /* Avanca 4017 para proximo display */

    if (display == 0)
    {
        RESET_4017();  /* Volta 4017 para Q0 (primeiro display) */

#ifndef CALIBRACAO
        {
            /* Unidade do minuto (ou alarme) */
            uint8_t val = BOTAO_ALARME_PRESSIONADO ? alarme_minuto : minuto;
            PORTB = obterNumeroDisplay(obterUnidade(val));
        }
#else
        /* Modo calibracao: mostra valor de cal nos displays */
        PORTB = obterNumeroDisplay(obterUnidade(cal));
#endif

        display++;
    }
    else if (display == 1)
    {
#ifndef CALIBRACAO
        {
            /* Dezena do minuto (ou alarme) */
            uint8_t val = BOTAO_ALARME_PRESSIONADO ? alarme_minuto : minuto;
            PORTB = obterNumeroDisplay(obterDecimal(val));
        }
#else
        PORTB = obterNumeroDisplay(obterDecimal(cal));
#endif

        display++;
    }
    else if (display == 2)
    {
        {
            /* Unidade da hora (ou alarme) */
            uint8_t val = BOTAO_ALARME_PRESSIONADO ? alarme_hora : hora;
            PORTB = obterNumeroDisplay(obterUnidade(val));
        }
        display++;
    }
    else if (display == 3)
    {
        {
            /*
             * Dezena da hora (ou alarme).
             * Se valor for 0, apaga o display (mostra 0xFF = todos apagados).
             * Isso evita mostrar "0" a esquerda (ex: "08:30" vira " 8:30").
             */
            uint8_t val = BOTAO_ALARME_PRESSIONADO ? alarme_hora : hora;
            uint8_t dh = obterDecimal(val);
            PORTB = (dh == 0) ? 0xFF : obterNumeroDisplay(dh);
        }
        display = 0;
    }
}

/*
 * Task3: acoes dos botoes (ajuste de tempo/alarme).
 * Executa a cada ~201ms (TEMPO_TASK3_DEF).
 *
 * Fluxo de cada botao:
 *   1. FLAG_BOTAO_SOLTO (255): botao acabou de ser liberado (toque rapido).
 *      Incrementa em 1 unidade.
 *   2. pushFlag > FLAG_BOTAO_SEGURO (~33): botao sendo segurado ha >2s.
 *      Incrementa continuamente (aceleracao).
 *
 * Prioridade: MIN antes de HORA (evita processar ambos ao mesmo tempo).
 *
 * Regras:
 *   - Se alarme tocando: qualquer botao pressionado desativa o alarme.
 *   - Se botao ALARME segurado: ajusta alarme (HORA/MIN).
 *   - Caso contrario: ajusta relogio (HORA/MIN).
 */
void task3(void)
{
    uint8_t pushFlagMinuto;
    uint8_t pushFlagHora;
    uint8_t processado;

    pushFlagMinuto = pushFlag[BOTAO_MINUTO];
    pushFlagHora = pushFlag[BOTAO_HORA];
    processado = 0;

    /*
     * === Botao MINUTO (RA2) ===
     */
    if (pushFlagMinuto == FLAG_BOTAO_SOLTO)
    {
        /*
         * Toque rapido (pressionou e soltou em <2s).
         * Limpa o flag e executa acao uma vez.
         */
        pushFlag[BOTAO_MINUTO] = FLAG_BOTAO_ND;
        processado = 1;

        if (toca_alarme > 0)
        {
            /*
             * DESATIVAR ALARME via botao MIN.
             *
             * NOTA: Devido ao compartilhamento RA2/speaker, este codigo
             * so e acessado se SPK_COMPARTILHADO_COM_BOTAO=0 ou se
             * o alarme nao esta tocando. Se SPK_COMPARTILHADO_COM_BOTAO=1,
             * o botao MIN e ignorado durante o alarme (ver task1).
             */
            toca_alarme = 0;
            alarme_bloqueado = 1;
        }
        else if (BOTAO_ALARME_PRESSIONADO)
        {
            /*
             * AJUSTE RAPIDO DO MINUTO DO ALARME.
             * Segura ALARME + toca MIN uma vez = incrementa 1 minuto.
             */
            alarme_minuto++;

            if (alarme_minuto > 59)
                alarme_minuto = 0;

            alarme_ajustando = 1;
            alarme_bloqueado = 1;
        }
        else
        {
            /*
             * AJUSTE RAPIDO DO MINUTO DO RELOGIO.
             * Toca MIN uma vez = incrementa 1 minuto, zera segundos.
             */
            segundo = 0;
            minuto++;

#ifdef CALIBRACAO
            cal++;
#endif

            if (minuto > 59)
                minuto = 0;
        }
    }
    else if (pushFlagMinuto > FLAG_BOTAO_SEGURO)
    {
        /*
         * Botao MIN sendo SEGURADO ha mais de 2s.
         * Incremento continuo (aceleracao).
         */
        processado = 1;

        if (toca_alarme > 0)
        {
            toca_alarme = 0;
            alarme_bloqueado = 1;
        }
        else if (BOTAO_ALARME_PRESSIONADO)
        {
            alarme_minuto++;

            if (alarme_minuto > 59)
                alarme_minuto = 0;

            alarme_ajustando = 1;
            alarme_bloqueado = 1;
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

    /*
     * === Botao HORA (RA4) ===
     *
     * So processa se nenhum outro botao ja foi processado (processado==0).
     * Isso evita que MIN e HORA ambos increments no mesmo ciclo.
     */
    if (!processado && (pushFlagHora == FLAG_BOTAO_SOLTO))
    {
        /*
         * Toque rapido no botao HORA.
         */
        pushFlag[BOTAO_HORA] = FLAG_BOTAO_ND;
        processado = 1;

        if (toca_alarme > 0)
        {
            /*
             * DESATIVAR ALARME via botao HORA.
             * HORA nao tem compartilhamento de pino, sempre funciona.
             */
            toca_alarme = 0;
            alarme_bloqueado = 1;
        }
        else if (BOTAO_ALARME_PRESSIONADO)
        {
            /*
             * AJUSTE RAPIDO DA HORA DO ALARME.
             * Segura ALARME + toca HORA uma vez = incrementa 1 hora.
             */
            alarme_hora++;

            if (alarme_hora > 23)
                alarme_hora = 0;

            alarme_ajustando = 1;
            alarme_bloqueado = 1;
        }
        else
        {
            /*
             * AJUSTE RAPIDO DA HORA DO RELOGIO.
             * Toca HORA uma vez = incrementa 1 hora.
             */
            hora++;

#ifdef CALIBRACAO
            cal--;
#endif

            if (hora > 23)
                hora = 0;
        }
    }
    else if (!processado && (pushFlagHora > FLAG_BOTAO_SEGURO))
    {
        /*
         * Botao HORA sendo SEGURADO ha mais de 2s.
         * Incremento continuo (aceleracao).
         */
        processado = 1;

        if (toca_alarme > 0)
        {
            toca_alarme = 0;
            alarme_bloqueado = 1;
        }
        else if (BOTAO_ALARME_PRESSIONADO)
        {
            alarme_hora++;

            if (alarme_hora > 23)
                alarme_hora = 0;

            alarme_ajustando = 1;
            alarme_bloqueado = 1;
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

/*
 * Funcao principal.
 *
 * Inicializacao:
 *   - Configura pinos (TRISA, TRISA)
 *   - Inicializa variaveis
 *   - Configura Timer0 para ~1ms
 *   - Habilita interrupcoes
 *
 * Loop principal:
 *   - Executa tasks quando seus contadores chegam a 0
 *   - Gerencia logica do alarme (disparo e desligamento)
 *   - Controla speaker quando alarme desligado
 */
void main(void)
{
    uint8_t i;

    /*
     * Calibracao do Timer0.
     * Medido com osciloscopio: cal=10 gera ~501Hz (periodo ~1.996ms).
     * Com prescaler 1:4 e 4MHz, Timer0 incrementa a cada 4us.
     * (256-10) * 4us = 984us ~ 1ms.
     * Ajustar conforme crystal utilizado.
     */
    cal = 10;

    /*
     * Configuracao de pinos:
     *   TRISB = 0x00 -> todos RB0-RB7 como saida (segmentos + PWM)
     *   TRISA = 0b00010100 -> RA2(0)=ent MIN, RA4(4)=ent HORA
     *                        RA0(0), RA1(1), RA3(3) como saida inicialmente
     *
     * RA3 e alternado entre saida/entrada ciclicamente pela task1
     * para ler botao ALARME sem conflito com LED ponto.
     */
    TRISB = 0;
    TRISA = 0b00010100;

    PORTB = 0;
    PORTA = 0;

#if !SPK_COMPARTILHADO_COM_BOTAO
    /*
     * Se o speaker for dedicado (nao compartilha pino com botao),
     * ja configura como saida e em nivel seguro.
     */
#if SPK_IDLE_HIGH
    BIT_SET(SPK_PORT, SPK_PIN);
#else
    BIT_CLEAR(SPK_PORT, SPK_PIN);
#endif
    BIT_CLEAR(SPK_PORT_DEF, SPK_PIN);
#endif

    /*
     * Timer0 configurado para ~1ms.
     *
     * Prescaler 1:4, fonte interna (Fosc/4 = 1MHz).
     * Taxa de incremento: 1MHz / 4 = 250kHz -> 4us por incremento.
     * Com cal=10: (256-10) * 4us = 984us ~ 1ms.
     * Ajuste fino feito por 'cal' (medido no osciloscopio).
     */
    OPTION_REG = (OPTION_REG & 0xC0) | 0x01;

    /* Inicializacao do relogio */
    segundo = 0;
    minuto = 0;
    hora = 0;

    tempo = 0;
    ponto_sw = 0;
    tick_segundo = 0;

    /* Inicializacao do alarme (desabilitado: hora=0, minuto=0) */
    alarme_minuto = 0;
    alarme_hora = 0;
    toca_alarme = 0;
    alarme_ajustando = 0;
    alarme_bloqueado = 0;

    /* Inicializacao dos contadores das tasks */
    tempo_task1 = TEMPO_TASK1_DEF;
    tempo_task2 = TEMPO_TASK2_DEF;
    tempo_task3 = TEMPO_TASK3_DEF;

    display = 0;

    /* Inicializacao do estado dos botoes */
    for (i = 0; i < TOTAL_BOTOES; i++)
    {
        pushFlag[i] = FLAG_BOTAO_ND;
        pushState[i] = NoPush;
    }

    /* Inicializa Timer0 com valor calibrado */
    TMR0 = cal;

    /* Liga PWM do display (RB3 = 1 inicialmente) */
    RB3 = 1;

    /* Habilita interrupcao do Timer0 e global */
    T0IE = 1;
    GIE = 1;

    /*
     * Loop principal.
     * Executa as 3 tasks ciclicamente e gerencia o alarme.
     */
    for (;;)
    {
        /* Task1: leitura de botoes + relogio */
        if (tempo_task1 == 0)
        {
            tempo_task1 = TEMPO_TASK1_DEF;
            task1();
        }

        /* Task2: multiplexacao do display */
        if (tempo_task2 == 0)
        {
            tempo_task2 = TEMPO_TASK2_DEF;
            task2();
        }

        /* Task3: acoes dos botoes */
        if (tempo_task3 == 0)
        {
            tempo_task3 = TEMPO_TASK3_DEF;
            task3();
        }

        /*
         * === LOGICA DO ALARME ===
         *
         * Condicoes para disparo:
         *   1. Alarme nao esta inativo (hora != 0 ou minuto != 0)
         *   2. Horario atual coincide com horario do alarme
         *   3. Alarme nao esta bloqueado (ja disparou neste minuto)
         *   4. Alarme nao esta sendo ajustado
         *   5. Botao ALARME nao esta pressionado
         *
         * O alarme dispara CONTINUAMENTE enquanto as condicoes se mantem.
         * O alarme toca por TEMPO_ALARM_SEG segundos (contagem regressiva).
         * Se TEMPO_ALARM_SEG=0, toca ate ser desligado manualmente.
         *
         * O alarme_bloqueado impede re-disparo no mesmo minuto.
         * Ele e limpo quando o horario nao mais coincide (minuto avanca).
         */
        if (ALARME_INATIVO)
        {
            /*
             * Alarme desabilitado (hora=0, minuto=0).
             * Garante que tudo esta desligado e desbloqueado.
             */
            toca_alarme = 0;
            alarme_bloqueado = 0;
            alarme_ajustando = 0;
        }
        else
        {
            if ((hora == alarme_hora) && (minuto == alarme_minuto))
            {
                /*
                 * Horario coincide com alarme.
                 * Dispara se todas as condicoes atenderem.
                 */
                if (!alarme_bloqueado &&
                    !alarme_ajustando &&
                    !BOTAO_ALARME_PRESSIONADO)
                {
                    toca_alarme = TEMPO_ALARM_SEG;
                    alarme_bloqueado = 1;

                    /*
                     * Configura RA2 como saida e ativa speaker.
                     * BIT_SET/SBIT_CLEAR em TRISA e PORTA sao feitos
                     * ANTES de setar toca_alarme para evitar race condition
                     * com a ISR (que pode alternar RA2 entre as operacoes).
                     */
                    BIT_SET(SPK_PORT, SPK_PIN);
                    BIT_CLEAR(SPK_PORT_DEF, SPK_PIN);
                }
            }
            else
            {
                /*
                 * Horario nao coincide: libera alarme para proximo gatilho.
                 * Quando o minuto avanca, alarme_bloqueado e limpo,
                 * permitindo novo disparo no proximo minuto correspondente.
                 */
                alarme_bloqueado = 0;
            }
        }

        /*
         * Desligar speaker quando alarme nao esta tocando.
         *
         * Configura RA2 como saida alta (PNP corte = buzzer off)
         * e depois como entrada (tri-state, mantem nivel alto via pull-up).
         *
         * O speaker so e desligado aqui. Enquanto toca_alarme > 0,
         * a ISR controla o speaker (alternancia para gerar som).
         */
        if (toca_alarme == 0)
        {
            BIT_SET(SPK_PORT, SPK_PIN);
            BIT_SET(SPK_PORT_DEF, SPK_PIN);
        }
    }
}
