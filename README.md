# GCode Viewer

Visualizador nativo em C++17, SDL3 e OpenGL. Foco inicial: Windows x64.
Sem Python, navegador, .NET ou runtime de linguagem interpretada.

## Executar

Abra `dist/gcode-viewer.exe`. Mantenha `SDL3.dll` na mesma pasta.
Arraste um GCode para a janela ou clique em **OPEN**. Tambem aceita um caminho
como argumento na linha de comando, incluindo nomes Unicode.

| Controle | Acao |
| --- | --- |
| Scroll | Zoom |
| Botao direito + arrastar | Orbitar |
| Botao esquerdo + arrastar | Pan |
| Slider inferior | Avancar/retroceder pelos movimentos |
| FIT / F / Home | Enquadrar modelo |
| TRAVEL / T | Exibir/ocultar deslocamentos sem extrusao |
| PLAY / Espaco | Reproduzir/pausar |
| Setas esquerda/direita | Voltar/avancar um movimento |
| Shift + setas | Voltar/avancar 100 movimentos |
| O | Abrir arquivo |
| Esc | Sair |

Verde: movimentos com extrusao positiva. Azul acinzentado: deslocamentos.
Marcador amarelo: posicao atual. O slider acompanha movimentos geometricos;
comentarios, aquecimentos e extrusoes estacionarias nao ocupam passos.
A linha original correspondente aparece no rodape. PLAY percorre movimentos
em aproximadamente 30 segundos; nao simula o tempo real de impressao.

## Performance e arquitetura

- Janela abre antes de carregar o arquivo.
- Parsing sequencial em uma thread de trabalho; sem regex ou arvore por linha.
- Trocar de arquivo cancela o carregamento anterior.
- Uma transferencia da geometria para um VBO da GPU; copia de vertices na CPU
  liberada depois do upload. Metadados dos movimentos permanecem na CPU.
- Slider altera somente a contagem de vertices de uma chamada `glDrawArrays`.
- Interface e fonte bitmap proprias; nenhuma biblioteca de UI, fontes ou matematica.
- Desenho somente quando a tela muda; loop aguarda eventos em repouso.
- OpenGL 3.3 com VBO, utilizando perfil de compatibilidade.
- Shader expande segmentos na GPU em faixas com aparencia cilindrica, iluminacao
  difusa, brilho especular, profundidade de superficie e atenuacao por distancia.
  Nao gera malhas de tubos na CPU. Espessura visual nominal de 0,22 mm para
  extrusao, com minimo em pixels para legibilidade; nao representa largura real
  de extrusao do slicer. Antialiasing MSAA 4x quando disponivel.
- Fonte bitmap alinhada a pixels fisicos inteiros, inclusive em telas com DPI alto.
- Executavel Windows GUI: nao abre janela de console.

Medicao local em Release: arquivo sintetico de 22.500.015 bytes com 999.999
movimentos geometricos interpretado em 0,215 s. Isso mede parsing, incluindo
leitura do arquivo, com cache do sistema possivelmente aquecido; nao inclui
upload para GPU nem representa garantia para outros arquivos ou computadores.
Executavel de aproximadamente 1,18 MB, mais SDL3.dll de aproximadamente 5,38 MB.

## GCode suportado

`G0/G1`, movimentos modais, `G2/G3` com I/J/K ou R positivo/negativo,
circulos completos IJK, arcos helicoidais, planos `G17/G18/G19`, `G20/G21`,
`G90/G91`, `G90.1/G91.1`, `G92`, `M82/M83`, troca de extrusor `T0..T255`,
comentarios `;` e `( )`, numeros de linha e sufixos de checksum.
Coordenadas e extrusao tem modos absoluto/relativo independentes.
Arcos sao aproximados por segmentos com tolerancia nominal de 0,05 mm;
ha limite de 100.000 segmentos por arco.

Este visualizador nao e um emulador de firmware. Homing, macros, offsets de
ferramenta, G54-G59, compensacao de mesa, G5 e comandos especificos de fabricantes
nao sao simulados fisicamente. Comandos G desconhecidos bloqueiam o movimento daquela linha;
o contador UNSUPPORTED sinaliza comandos nao reconhecidos. Arcos inconsistentes
viram uma linha reta e incrementam INVALID ARCS. Confira esses contadores ao
inspecionar um arquivo. Comandos M usuais de temperatura/ventoinha sao ignorados.
O checksum e delimitador, sem validacao de integridade. Valores F nao alteram PLAY.
Arquivos CNC sem E aparecem como deslocamentos; mantenha TRAVEL habilitado.

### Comandos de inicializacao e configuracao do slicer

- `G28`: referencia todos os eixos ou somente X/Y/Z indicados, inclusive eixos
  sem valor (`G28 X Y`). Como nao ha perfil de maquina, assume home em zero e
  limpa os offsets G92 desses eixos. Nao desenha um trajeto ficticio de homing.
  `G28 O` pula a referencia quando os eixos selecionados ja foram referenciados.
- `M73 P/R`: registra progresso e minutos restantes informados pelo slicer.
  Nao reposiciona o slider, que continua seguindo os movimentos geometricos.
- `M201`, `M203`, `M204`, `M205`: registra limites de aceleracao/velocidade,
  aceleracoes de impressao/retracao/travel e parametros de jerk.
- `M220`: registra percentual de velocidade (e backup/restore B/R).
- `M221 S/T`: aplica estado de fluxo por extrusor. Fluxo zero classifica o
  movimento como deslocamento; percentuais positivos preservam a classificacao
  de extrusao. Nao altera as coordenadas logicas E nem a espessura visual fixa.
- `M900 K/T`: registra Linear Advance por extrusor.

Esses comandos sao reconhecidos e deixam de aparecer no log UNSUPPORTED.
Os parametros finais ficam em `Model::settings`; aceleracao, jerk, velocidade
e pressao nao sao simulados, e PLAY continua sem representar tempo real.
Sem perfil de maquina, o home zero e uma aproximacao; maquinas com home no
maximo ou offsets de firmware podem precisar de configuracao adicional.

Semantica baseada na [documentacao de GCode do Marlin](https://marlinfw.org/meta/gcode/).

## Log de comandos nao suportados

Ao abrir `peca.gcode`, o aplicativo grava `peca.gcode.unsupported.log` na mesma
pasta, em UTF-8. Cada ocorrencia inclui numero da linha fisica do arquivo,
comando G/M e linha original completa, por exemplo:

```text
12 | G59.1 | G59.1 ; workspace
45 | M999 | M999
```

O log corresponde ao contador UNSUPPORTED e e regravado a cada abertura.
Comandos suportados/ignorados conhecidos nao entram no log. Mesmo sem ocorrencias,
o arquivo tem cabecalho e total zero. O rodape COMPLETE confirma que a leitura
terminou; carregamentos cancelados podem deixar um log parcial sem esse rodape.
Se a pasta nao permitir escrita, a visualizacao continua e um aviso informa
que o log nao pode ser salvo.

## Compilar no Windows com o ambiente portatil deste projeto

```powershell
./build.ps1
./dist/gcode-viewer.exe examples/demo.gcode
```

O script usa GCC 15 do w64devkit em `.tools/w64devkit` e SDL3 3.4.18 em
`vendor/SDL3-3.4.18`. Nao instala ferramentas globalmente. `./build.ps1 -Debug`
preserva simbolos e desativa otimizacoes. O toolchain e o pacote de desenvolvimento
nao sao necessarios para executar ou distribuir o programa.

## Compilar via CMake

Instale um compilador C++17, CMake e o pacote de desenvolvimento SDL3.
Depois execute (ajuste `CMAKE_PREFIX_PATH` para a instalacao do SDL3):

```text
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=/caminho/SDL3
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

CMake e codigo usam APIs multiplataforma. Windows x64 foi compilado e testado;
Linux ainda nao foi validado. O renderer atual exige OpenGL 3.3 compatibility;
macOS precisara de adaptacao para perfil core antes de ser suportado.

## Verificacao

```powershell
./dist/parser-tests.exe
./dist/parser-tests.exe --benchmark
./dist/gcode-viewer.exe examples/demo.gcode --smoke
./dist/gcode-viewer.exe examples/demo.gcode --selftest
```

O benchmark gera e remove um arquivo sintetico de um milhao de linhas.
O selftest injeta eventos SDL de zoom, orbita, pan, slider, filtro de travel e
drop/reabertura; verifica o estado resultante e grava `dist/selftest-half.bmp`
e `dist/smoke.bmp`. Renderizacao verificada localmente em OpenGL 4.6/NVIDIA.
Nao equivale a um teste manual de arrastar pelo Explorer ou do seletor de arquivos.

SDL3: <https://libsdl.org/> — licenca zlib incluida em `dist/SDL-LICENSE.txt`.
w64devkit: <https://github.com/skeeto/w64devkit> — somente ferramenta de build.
