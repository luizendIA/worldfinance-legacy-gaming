# World Finance Legacy Gaming

**Jogos de última geração. Hardware antigo. Inovação de código aberto.**

Laboratório independente de computação gráfica idealizado e patrocinado pela World Finance. Publicamos código, medições e imagens de referência para que outras pessoas possam repetir os testes em hardware acessível. [English](README.md) · [Site em português](https://luizendIA.github.io/worldfinance-legacy-gaming/pt.html) · [Comunidade](https://github.com/luizendIA/worldfinance-legacy-gaming/discussions)

### Primeiro experimento

Na Radeon RX 570 física, nossa cena de terreno levou **4,343 ms** em 1280 × 720 e **1,505 ms** em 640 × 360 com ampliação bilinear para 1280 × 720. Cada valor é a mediana das medianas de quatro execuções. O erro quadrático médio em raiz (RMSE) nos canais RGB foi de 7,613 em uma escala de 0 a 255. O driver expôs Shader Model 6.5 e Mesh Shader Tier 0. GTA VI segue sem teste neste equipamento. [Dados e matriz de hardware](benchmarks/README.md) · [Viabilidade da RX 570](reports/rx570.md)

### Como repetir

Em Windows x64, instale o conjunto C++ para desktop do Visual Studio 2022 Build Tools, um Windows SDK e CMake 3.20 ou superior. O programa usa o primeiro adaptador físico listado pelo DXGI. Confira o nome no JSON.

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
.\build\Release\legacy-lab.exe | Out-File -Encoding utf8 rodada-1.json
.\build\Release\legacy-lab.exe --reverse | Out-File -Encoding utf8 rodada-2.json
python tools/summarize.py rodada-1.json rodada-2.json
```

O modo `--smoke` valida uma imagem pequena. `--warp` seleciona o renderizador por software. Para gravar a imagem nativa, use `--capture terreno.ppm` com um caminho novo. [Compilação e SHA-256](reports/build.md)

### Origem, autoria e próximos passos

O VIctory é obra de Marvisak e dos colaboradores do projeto original. A política de materiais do upstream exclui uma tabela BCPack da licença do repositório. Por isso, o laboratório tem histórico próprio, renderizador original e uma referência permanente ao commit analisado. A iniciativa é independente da Rockstar Games. O código e os materiais gerados pelo laboratório são distribuídos sob GPL-3.0-ou-posterior. [Auditoria](reports/upstream-review.md) · [Licença](LICENSE) · [Roadmap](ROADMAP.md) · [Histórico](CHANGELOG.md) · [Como contribuir](CONTRIBUTING.md)

**Uma iniciativa apoiada pela World Finance**

A World Finance apoia a inovação de código aberto e a pesquisa tecnológica para tornar os jogos modernos mais acessíveis, inclusive para pessoas que não possuem condições financeiras de adquirir computadores de alto desempenho.

Conheça a World Finance e descubra como apoiar voluntariamente nossas pesquisas. Acesso e participação são gratuitos. [Área de apoio](https://luizendIA.github.io/worldfinance-legacy-gaming/pt.html#support)

[Memória de vídeo da RX 570 antes, durante e depois da renderização](benchmarks/vram-rx570/README.md)
