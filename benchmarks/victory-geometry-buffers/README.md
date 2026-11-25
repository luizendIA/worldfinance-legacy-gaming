# VIctory Polaris. Captura de geometria em 720p

WordFinance · 25 de novembro de 2026

Após compilar, abra **polaris-720p.exe** com dois cliques. Um diálogo mostra o resultado e fica aberto até você pressionar **OK**. Para receber o resumo JSON em um terminal, execute `polaris-720p.exe --cli`.

## O que a RX 570 executou

O tradutor modificado de VIctory recebe um programa GFX10 sintético e passa pelas etapas de análise de instruções, representação intermediária, estruturação e emissão DXIL. Em modo de captura, emite um compute shader SM 6.5. A GPU grava posições `float4` em um buffer e índices `uint32` em outro. O programa Windows transiciona esses recursos para uso como buffers de vértices e índices. O Direct3D 12 então executa `DrawIndexedInstanced` via `ExecuteIndirect`, com o número de índices da cena sintética.

A GPU física produziu exatamente os **32 vértices e 96 índices** previstos. Nas quatro execuções em 1280 × 720, o programa verificou os 921.600 IDs e valores de profundidade, tanto no caso visível quanto no caso em que a flag de descarte está ativa. **Zero divergências** nas duas verificações e nos buffers. Também testei a abertura do diálogo e o encerramento após fechá-lo. Nesta máquina, o renderizador WARP oferece SM 6.2. O programa avisa quando o adaptador oferece menos que o SM 6.5 usado neste teste. [Dados brutos](resultados-rx570.json)

O caso é restrito a um grupo de 32 threads, geometria sintética, exportação de posição, exportação de um triângulo por thread e dois buffers de saída. O patch mantém o caminho normal de mesh shaders do upstream. A integração no jogo ainda exige lidar com múltiplos grupos, saídas por vértice e primitiva, atributos, recursos, sincronização e chamadas indiretas reais. Esta execução não carregou GTA VI. [Revisão de referência do VIctory](https://github.com/Marvisak/VIctory/tree/940d32f69b22265690a0b5a15d6ded572ba33baf)

## Compilação Windows

Use o Visual Studio 2022 Build Tools com o compilador C++, o Windows SDK e o CMake. Os dois módulos DXIL incluídos são gerados a partir da cena sintética e validados pelo SDK.

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
.\build\Release\polaris-720p.exe --cli
```

Para regenerar o DXIL, aplique `victory-geometry-buffers.patch` à revisão de referência. Execute `cargo run --release -p victory-shader --example export_polaris -- generated` no checkout do VIctory. O comando gera `visible.dxil` e `culled.dxil` numa pasta nova. Compile o validador com `cl /EHsc /std:c++17 sign-dxil.cpp` no terminal x64 do Visual Studio. Use `sign-dxil.exe SDK_BIN INPUT.dxil OUTPUT.dxil` para validar cada módulo e gravar a versão assinada nesta pasta, com os nomes `visible.dxil` e `culled.dxil`. `SDK_BIN` designa o diretório x64 do Windows SDK que contém `dxcompiler.dll` e `dxil.dll`.

| Artefato da execução de referência | SHA-256 |
| --- | --- |
| Executável Windows x64 | `dd18a674da7f3094638def307a6db508d7fdeb662b2a341b479d8cdae9d7278f` |
| Shader visível validado | `d30af2d472a3ff7bf5b059c7bf653920e3171358e86d64d41b9cc64d6166d283` |
| Shader descartado validado | `08789b1f899d0819001644580586744b925cbc3502d3b753c6701e1127dec6a3` |

Os dois módulos foram emitidos com Rust 1.98.0 em WSL isolado e validados no Windows SDK 10.0.26100.0. O executável foi compilado em Release com MSVC 19.44.35229. Código e fixtures sintéticas distribuídos sob GPL-3.0-or-later. O programa de teste GFX10 é derivado do fixture sintético da revisão do VIctory indicada acima.
