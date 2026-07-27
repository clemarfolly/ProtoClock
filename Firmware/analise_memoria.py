import sys
import re

def analisar_map(arquivo_map):
    with open(arquivo_map, 'r') as f:
        linhas = f.readlines()

    # Encontrar a secao "Section Info"
    inicio = None
    for i, l in enumerate(linhas):
        if 'Section Info' in l:
            inicio = i + 3  # pular header e separador
            break
    if inicio is None:
        print("Erro: secao 'Section Info' nao encontrada no .map")
        sys.exit(1)

    # Parse das secoes code/program
    rom_codigo = 0      # codigo real (exclui .fill_ e .config_)
    rom_preenchimento = 0
    rom_config = 0
    ram_total = 0
    secoes_codigo = []

    for l in linhas[inicio:]:
        l = l.rstrip()
        if not l or l.startswith('---') or 'Program Memory' in l or 'Symbols' in l:
            if 'Program Memory' in l or 'Symbols' in l:
                break
            continue

        # Tentar fazer parse da linha
        # Formato: NOME  TYPE  ADDR  LOCATION  SIZE(HEX)
        m = re.match(r'\s*(\S+)\s+(code|udata)\s+(0x[0-9a-fA-F]+)\s+(program|data)\s+(0x[0-9a-fA-F]+)', l)
        if not m:
            continue

        nome = m.group(1)
        tipo = m.group(2)
        addr = int(m.group(3), 16)
        location = m.group(4)
        tam_bytes = int(m.group(5), 16)
        tam_words = tam_bytes // 2

        if tipo == 'code' and location == 'program':
            if '.fill_' in nome:
                rom_preenchimento += tam_words
            elif '.config_' in nome:
                rom_config += tam_words
            else:
                rom_codigo += tam_words
                secoes_codigo.append((nome, addr, tam_words))

        elif tipo == 'udata' and location == 'data':
            # Excluir SFRs mapeados (UD_abs_pic16f84_*)
            if 'UD_abs_pic16f84_' in nome:
                continue
            ram_total += tam_bytes

    # Calcular endereco final do codigo
    if secoes_codigo:
        # Ordenar por endereco
        secoes_codigo.sort(key=lambda x: x[1])
        ultimo_nome, ultimo_addr, ultimo_tam = secoes_codigo[-1]
        fim_codigo = ultimo_addr + ultimo_tam - 1
    else:
        fim_codigo = 0

    rom_total = 1024

    # Percentuais
    pct_codigo = (rom_codigo / rom_total) * 100
    pct_livre = ((rom_total - rom_codigo) / rom_total) * 100

    # Saida
    print()
    print('=' * 50)
    print('  PIC16F84 - Analise de Memoria')
    print('=' * 50)
    print()
    print(f'  ROM total:   {rom_total} words (0x000-0x3FF)')
    print(f'  RAM total:   68 bytes  (0x0C0-0x04F)')
    print()
    print('--- Program Memory ---')
    print(f'  Codigo real:     {rom_codigo:>4d} words  ({pct_codigo:.1f}% da ROM)')
    print(f'  Endereco final:  0x{fim_codigo:03X}')
    print(f'  Espaco livre:    {rom_total - rom_codigo:>4d} words  ({pct_livre:.1f}%)')
    print(f'  Preenchimento:   {rom_preenchimento:>4d} words  (.fill_1)')
    print(f'  Config word:     {rom_config:>4d} word   (0x2007)')
    print(f'  Total gplink:    {rom_codigo + rom_preenchimento + rom_config:>4d} words  (reportado pelo linker)')
    print()

    # Top 10 maiores secoes
    secoes_ordenadas = sorted(secoes_codigo, key=lambda x: x[2], reverse=True)
    print('  Top secoes por tamanho:')
    for nome, addr, tam in secoes_ordenadas[:10]:
        # Simplificar nome: remover prefixo S_ e sufixo de arquivo
        m = re.match(r'^S_.*?__([a-zA-Z_]\w*)$', nome)
        if m:
            nome_curto = m.group(1)
        elif nome.startswith('IDC_'):
            nome_curto = 'table_' + nome.split('_')[-1]
        elif nome.startswith('.'):
            nome_curto = nome
        else:
            nome_curto = nome
        if len(nome_curto) > 25:
            nome_curto = nome_curto[:22] + '...'
        print(f'    {nome_curto:<28s} 0x{addr:03X}  {tam:>3d}w')
    print()

    print('--- Data Memory (udata) ---')
    print(f'  RAM utilizada:  {ram_total:>3d} bytes / 68 bytes')
    print()

if __name__ == '__main__':
    if len(sys.argv) < 2:
        print(f'Uso: {sys.argv[0]} <arquivo.map>')
        sys.exit(1)
    analisar_map(sys.argv[1])
