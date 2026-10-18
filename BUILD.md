# Compilar e testar

## Fonte correspondente

Clone este fork com os submódulos e selecione a tag da release:

```powershell
git clone --recursive https://github.com/vnzin-rb/shadps4-infamous-keyboard.git
cd shadps4-infamous-keyboard
git checkout v1.0.0
git submodule update --init --recursive
```

Siga também [as instruções Windows do projeto](documents/building-windows.md) para as dependências. O pacote publicado foi compilado em Release, usando clang-cl 23.1.3, MSVC 14.44.35207 e Windows SDK 10.0.26100.0. O projeto usa x86-64-v3/AVX2. Não foi alterada a lógica de desempenho da base AYOUB.

Em um terminal com o ambiente de compilação MSVC/Windows SDK carregado e clang-cl, CMake e Ninja no PATH:

```powershell
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=clang-cl -DCMAKE_CXX_COMPILER=clang-cl
cmake --build build --target shadps4 --parallel 4
```

Para aplicar os controles no executável compilado, copie os arquivos de `package/spray-profile/input_config` para `user/input_config` ao lado do executável. O diretório de trabalho deve ser a pasta do executável.

## Testes de controles

A suíte isolada compila a implementação real do controle, a leitura dos mapeamentos e o empacotamento dos relatórios de pad. Usa substitutos para relógio, GUI e hardware; não acessa saves ou perfis reais.

```powershell
cmake -S tests/touchpad -B build-touchpad -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=clang-cl
cmake --build build-touchpad --parallel 4
ctest --test-dir build-touchpad --output-on-failure -V
```

Resultado da publicação: **17.702 verificações, 13 cenários, todos aprovados**. Os testes cobrem os dois dedos, quatro direções, tempo/continuidade dos gestos, aliases, liberação, perda de foco, relatórios guest e movimento/spray. Testes automáticos não substituem jogar cada interação.

## Organização das alterações

- `src/input/controller.*`: contatos independentes, gestos temporizados e movimento virtual.
- `src/input/input_handler.*`: novos comandos configuráveis e mapeamentos.
- `src/input/input_mouse.cpp`: sincronização dos modos de mouse e liberação do giroscópio.
- `src/sdl_window.cpp`: liberação de entradas ao perder o foco.
- `tests/touchpad`: testes isolados.
- `package/spray-profile`: perfil público sem dados pessoais.
- `distribution`: iniciador e instruções do pacote.

A implementação dos gestos e sensores é separada das otimizações da AYOUB. Cada gesto usa um único contato contínuo durante 300 ms e mantém a posição final enquanto a tecla fica pressionada.
