# ProtoClock

Relógio digital com alarme para **PIC16F84**, construído com componentes simples e disponíveis.

![PIC16F84](https://img.shields.io/badge/Microcontroller-PIC16F84-blue)
![SDCC](https://img.shields.io/badge/Compiler-SDCC_4.3.0-green)

## Funcionalidades

- Relógio com 4 displays de 7 segmentos (HH:MM)
- Ponto decimal piscando (1Hz)
- Alarme configurável (hora e minuto)
- Brilho dos displays controlado por PWM
- Calibração fina do timer via osciloscópio

## Hardware

### Componentes

| Componente | Descrição | Quantidade |
|------------|-----------|------------|
| PIC16F84 | Microcontrolador | 1 |
| 4017 | Decodificador decimal | 1 |
| 74HC08 | Quad AND gate | 1 |
| BC558 | Transistor PNP | 1 |
| LTS-6960HR | Display 7 segmentos | 4 |
| BZ1 | Buzzer | 1 |
| Y1 | Cristal 4MHz | 1 |
| R1-R3 | 10kΩ (pull-up botões) | 3 |
| R4 | 1kΩ (base transistor) | 1 |
| R5-R12 | 220Ω (segmentos) | 8 |
| C1-C2 | 22pF (cristal) | 2 |
| C3-C4 | 100nF (desacoplamento) | 2 |
| C5 | 10µF (alimentação) | 1 |
| D1-D2 | LED (ponto decimal) | 2 |
| D3 | 1N4007 (proteção buzzer) | 1 |

### Pinagem

```
PIC16F84
┌────────────────────┐
│ 1 RA2   SPK/MIN   │ ← Speaker + Botão MIN (compartilhado)
│ 2 RA3   PONTO/ALR  │ ← LED Ponto + Botão ALARME (compartilhado)
│ 3 RA4   HORA       │ ← Botão HORA
│ 4 MCLR  RESET      │ ← Reset (10k pull-up)
│ 5 VSS   GND        │
│ 6 RB0              │ → Segmento a
│ 7 RB1              │ → Segmento b
│ 8 RB2              │ → Segmento c
│ 9 RB3              │ → PWM Brilho (74HC08)
│ 10 RB4             │ → Segmento d
│ 11 RB5             │ → Segmento e
│ 12 RB6             │ → Segmento f
│ 13 RB7             │ → Segmento g
│ 14 VDD   +5V       │
│ 15 OSC2            │ ← Cristal 4MHz
│ 16 OSC1            │ ← Cristal 4MHz
│ 17 RA0             │ → 4017 CLK
│ 18 RA1             │ → 4017 RESET
└────────────────────┘
```

### Esquemático

O esquemático completo está em [`ProtoClock.pdf`](ProtoClock.pdf).

## Firmware

### Arquivos

| Arquivo | Descrição |
|---------|-----------|
| `ProtoClock84_Otimizado.c` | Código fonte principal (PIC16F84) |
| `ProtoClock.c` | Código original (PIC16F628A, referência) |
| `c84_Otimizado.bat` | Script de compilação |
| `analise_memoria.py` | Análise de uso de memória |

### Estrutura do Código

```
├── Macros e Defines
│   ├── BIT_SET/CLEAR/FLIP/CHECK — manipulação de bits
│   ├── RESET_4017/CLOCK_4017 — controle do decodificador
│   └── SPK_* — configuração do speaker
│
├── Variáveis Globais
│   ├── Relógio: segundo, minuto, hora
│   ├── Alarme: alarme_minuto, alarme_hora, toca_alarme
│   └── Botões: pushFlag[], pushState[]
│
├── ISR (Interrupção)
│   └── Timer0 ~1ms: contagem de tempo, PWM, speaker
│
├── task1() — 61ms
│   ├── Contagem de tempo (segundo/minuto/hora)
│   ├── Leitura de botões com debounce
│   └── Desligamento do alarme
│
├── task2() — 5ms
│   └── Multiplexação dos displays (4017)
│
├── task3() — 201ms
│   └── Ações dos botões (ajuste tempo/alarme)
│
└── main()
    ├── Inicialização
    ├── Loop principal
    └── Lógica do alarme
```

### Compilação

**Requisitos:**
- [SDCC 4.3.0](http://sdcc.sourceforge.net/) (compilador C para PIC)
- [gputils](http://gputils.sourceforge.net/) (montador e linker)

**Passos:**

```bash
# Compilar (Windows)
c84_Otimizado.bat

# Ou manualmente:
sdcc -S --use-non-free -mpic14 -p16f84 ProtoClock84_Otimizado.c
gpasm -I C:\Programas\gputils\header -o ProtoClock84_Otimizado.o -c ProtoClock84_Otimizado.asm
gplink -O 2 -f 0x00 -r -m -s C:\Programas\gputils\lkr\16f84_g.lkr -o ProtoClock84_Otimizado.hex ProtoClock84_Otimizado.o C:\Programas\SDCC\lib\pic14\libsdcc.lib C:\Programas\SDCC\non-free\lib\pic14\pic16f84.lib
```

**Análise de memória:**

```bash
python analise_memoria.py ProtoClock84_Otimizado.map
```

Saída:
```
--- Program Memory ---
  Codigo real:      930 words  (90.8% da ROM)
  Espaco livre:      94 words  (9.2%)

--- Data Memory ---
  RAM utilizada:   57 bytes / 68 bytes
```

### Calibração do Timer

O cristal de 4MHz não é preciso. O Timer0 precisa de calibração fina:

1. Defina `#define CALIBRACAO 1` no código
2. Compile e grave no PIC
3. Meça a frequência no pino RA3 com osciloscópio
4. Ajuste o valor de `cal` em `main()` até obter ~500Hz (2ms período)
5. Remova `#define CALIBRACAO 1`
6. Recompile

Valor padrão: `cal = 10` (ajustado para ~501Hz)

## Botões

| Botão | Pino | Função |
|-------|------|--------|
| HORA | RA4 | Ajustar hora / Desligar alarme |
| MIN | RA2 | Ajustar minuto / Desligar alarme* |
| ALARME | RA3 | Segurar para ver/ajustar alarme |

### Operação

**Ver horário:** Exibição padrão nos displays.

**Ajustar relógio:**
- Toque rápido em HORA ou MIN = incrementa 1
- Segurar HORA ou MIN = incremento rápido

**Configurar alarme:**
1. Segurar botão ALARME
2. Enquanto segura, tocar HORA ou MIN para ajustar
3. Displays mostram o horário do alarme
4. Soltar ALARME volta ao horário

**Alarme desabilitado:** Hora = 0 e Minuto = 0

**Desligar alarme:** Pressionar HORA ou ALARME enquanto toca.

> \* **Nota:** O botão MIN compartilha pino RA2 com o speaker. Enquanto o alarme toca, o MIN não funciona. Apenas HORA e ALARME podem desligar.

## Limitações de Hardware

### Pinos Compartilhados

| Pino | Funções | Conflito |
|------|---------|----------|
| RA2 | Speaker + Botão MIN | Botão MIN inoperante durante alarme |
| RA3 | LED Ponto + Botão ALARME | LED desligado durante leitura do botão |

### Soluções implementadas

- **RA2:** Botão MIN é ignorado enquanto `toca_alarme > 0`
- **RA3:** Pin é alternado entre saída (LED) e entrada (botão) ciclicamente a cada ~61ms

### Recomendações para versão futura

- Usar pinos dedicados para speaker e botões
- Adicionar capacitor 100nF entre MCLR e GND para reset mais confiável
- Considerar PIC16F628A (mais pinos, BOR interno)

## Memória

| Recurso | Total | Utilizado | Livre |
|---------|-------|-----------|-------|
| ROM (programa) | 1024 words | 930 words | 94 words |
| RAM (dados) | 68 bytes | 57 bytes | 11 bytes |

### Distribuição da ROM

| Seção | Tamanho | Descrição |
|-------|---------|-----------|
| task1 | 286w | Leitura de botões + relógio |
| task3 | 164w | Ações dos botões |
| main | 145w | Loop principal + alarme |
| ISR | 129w | Interrupção (Timer0) |
| task2 | 124w | Multiplexação display |
| .code | 19w | Biblioteca _gptrget1 |
| obterNumeroDisplay | 18w | Conversão display |
| obterDecimal | 15w | Extração dezena |
| obterUnidade | 13w | Extração unidade |
| numero[10] | 10w | Tabela de segmentos |
| botaoIO[3] | 3w | Mapeamento botões |

## Esquemático

O esquemático completo está disponível em [`ProtoClock.pdf`](ProtoClock.pdf).

Feito com KiCad E.D.A. 9.0.7.

## Licença

Este é um projeto pessoal. Use como quiser.
