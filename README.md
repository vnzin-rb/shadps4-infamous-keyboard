# shadPS4 inFAMOUS — teclado e touchpad

Build Windows baseada na [AYOUB1080p](https://github.com/weshayoub92/shadPS4-AYOUB1080p), com dois dedos independentes, deslizes pelo teclado e emulação de movimento para o spray em **inFAMOUS Second Son**.

**[Baixar a build para Windows x64](https://github.com/vnzin-rb/shadps4-infamous-keyboard/releases/latest/download/shadps4-infamous-keyboard-windows-x64.zip)** · **[Guia rápido dos controles](GUIA-RAPIDO.md)**

## Como abrir

1. Baixe o ZIP pelo link acima e extraia a pasta inteira.
2. Abra **Iniciar_Jogo.cmd** e selecione o `eboot.bin` do seu jogo.
3. Se solicitado pelo emulador, configure os módulos de sistema extraídos do seu próprio PS4. O pacote não inclui jogo, firmware, módulos, chaves nem saves.

Também é possível selecionar o `shadPS4.exe` deste pacote no seu QtLauncher. Nesse caso, use os arquivos de `user/input_config` deste pacote na pasta de usuário ativa do launcher, fazendo backup dos seus mapeamentos antes.

## Touchpad: teclas principais

Use os números da **fileira superior do teclado**, não o teclado numérico. Segure a tecla por cerca de meio segundo. Se o jogo pedir para manter o dedo, continue segurando; solte ao concluir a ação.

| Ação | Tecla |
| --- | --- |
| Deslizar para cima | **1** |
| Deslizar para baixo / destruir torre de tiro | **2** ou **P** |
| Deslizar para a esquerda | **3** |
| Deslizar para a direita | **4** |
| Primeiro dedo / toque esquerdo | **Delete** |
| Segundo dedo / toque direito | **Page Down** |
| Dois dedos simultâneos | **Delete + Page Down**, segurando juntos |
| Toque central | **Espaço** |

Os deslizes funcionam diretamente pelo teclado, sem precisar clicar na seta da tela nem ativar o mouse. As setas ↑ ↓ ← → continuam sendo o direcional do controle.

## Spray

1. Segure **G** para virar o controle de lado.
2. Continue com G e segure **H** por 1–2 segundos para chacoalhar.
3. Solte H quando liberar a pintura; continue com G.
4. Aperte **F6** uma vez para mirar com o movimento do mouse.
5. Segure **O** (letra O, R2) e mova o mouse para pintar.
6. Ao terminar, solte O e G; aperte F6 novamente para desligar a mira pelo mouse.

**Insert** alterna o modo touchpad pelo mouse. **F6** alterna o giroscópio pelo mouse. G, H e os gestos do touchpad são comandos de manter pressionado.

## O que foi verificado

- Dois contatos independentes, identificação dos dedos, pressionar/segurar/soltar e relatórios recebidos pelo jogo.
- Quatro direções de deslize, duração de 300 ms, continuidade do contato, aliases de tecla e liberação ao perder o foco.
- Comandos de spray e preservação dos controles existentes.
- **17.702 verificações automáticas passaram**. O deslize para baixo na torre de tiro foi confirmado pelo usuário em CUSA00004; os demais atalhos direcionais novos foram verificados automaticamente e ainda precisam de validação em cada interação do jogo.

O ZIP contém o core e um iniciador simples; o QtLauncher pode ser obtido separadamente no [projeto oficial](https://github.com/shadps4-emu/shadps4-qtlauncher/releases). Requer Windows x64, CPU x86-64-v3/AVX2 e hardware compatível com shadPS4. Desempenho e compatibilidade variam conforme o computador e a versão do jogo.

## Código, créditos e licença

Este repositório é um fork da AYOUB no commit `153a5aa7f6b6be0e547d46719b793cb5145571d7`. Mantém as otimizações dessa base e acrescenta os controles acima. O [README original da AYOUB](README-AYOUB.md) foi preservado.

Créditos: [equipe shadPS4](https://github.com/shadps4-emu/shadPS4), [Ayoub](https://github.com/weshayoub92) e contribuidores. Adaptação dos controles publicada por [vnzin-rb](https://github.com/vnzin-rb).

Licença **GPL-2.0-or-later**; veja [LICENSE](LICENSE) e [LICENSES](LICENSES). Projeto independente, sem vínculo com Sony ou Sucker Punch. Para compilar e rodar os testes, veja [BUILD.md](BUILD.md). O código correspondente ao pacote está na tag da release.
