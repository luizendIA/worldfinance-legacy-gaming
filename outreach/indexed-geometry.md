# Four triangles on a 4 GB Radeon

**WordFinance research update · 24 November 2026**

Can a graphics card without mesh shaders draw geometry described as a meshlet? For one small, fully specified meshlet, a Radeon RX 570 did. We translated its local vertex indices into an ordinary indexed draw and asked an independent CPU implementation what each pixel should contain.

All four physical runs matched the CPU on coverage and visible triangle identity at every one of 65,536 pixels. The largest depth difference was 0.0000000596. The experiment covers a fixed meshlet, two overlapping surfaces, a shared edge and a back-facing triangle. GTA VI compatibility and general mesh shader translation remain open research questions.

Owners of another Polaris board can help by building the fixture, running the same capture and posting adapter, driver, OS, raw timings and comparison output. The readback can also be checked offline with Python. [Images, measurements and reproduction instructions](../benchmarks/indexed-rx570/README.md)

## Em português

Uma RX 570 de 4 GB desenhou uma cena de geometria indexada sem exigir mesh shaders. Usamos uma referência independente na CPU para conferir inclusive qual triângulo deveria aparecer quando duas superfícies se sobrepunham. Nas quatro rodadas, os 65.536 pixels concordaram em cobertura e identificação do triângulo visível.

Publicamos os buffers para quem quiser verificar o resultado em casa. Procuramos uma segunda placa Polaris para repetir o teste com outro driver. [Veja as imagens e repita o experimento](../benchmarks/indexed-rx570/README.md)

WordFinance
