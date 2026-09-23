# Aprender com o RenderLift — trilha em 5 camadas

O RenderLift foi desenhado também como sala de aula: cada camada do produto
corresponde a uma disciplina clássica de graphics engineering. Sugestão de
ordem, com o código do repositório como laboratório. Nada aqui exige um jogo
real — os suites de teste e o capture simulado são o "arcade mode".

## Camada 0 — C++ moderno aplicado

**Objetivo**: ler qualquer ficheiro do repo sem hesitar.

- O que usamos muito: `std::variant` + `std::visit` + `if constexpr`
  ([CaptureFormat.cpp](../../backends/common/src/obs/CaptureFormat.cpp)),
  `enum class`, RAII para COM/Win32 (`unique_ptr`, handles),
  `max_align` de políticas como `SteeringPolicy`.
- Por que a vtable é **read-only** e o que aconteceu quando tentámos
  escrevê-la ([VTable.hpp](../../backends/common/include/renderlift/backend/VTable.hpp),
  ADR 0002): páginas `.rdata`, `VirtualProtect`, e porque o compilador
  pode desvirtualizar chamadas.
- Exercício: escreve um tokenizador novo para `RLCAP1` e mede-o contra o
  atual com `RenderLift.CLI inspect`.

## Camada 1 — Graphics pipeline (independente de API)

**Objetivo**: ver um frame como uma sequência de passes, não como mágica.

- Conceitos: render target, swapchain, double buffering, MRT (deferred),
  cadeia HDR→LDR (tonemapping), MSAA, UI overlay, DRS (dynamic resolution
  scaling — o que a Xbox/PS já fazem).
- No repo: [`docs/graphics/`](../graphics/README.md) + o mapa de recursos
  produzido por `RenderLift.CLI inspect` sobre o
  [capture simulado](../research/samples/gta-v-simulated.rlcap) — cada classe
  do inspector corresponde a um conceito desta camada.

## Camada 2 — D3D11 como aplicação (sem hooks)

**Objetivo**: desenhar um triângulo e uma fullscreen pass "à mão".

- Device/Context/SwapChain, `CreateTexture2D`, RTV/DSV/SRV, `DrawIndexed`,
  um pixel shader de upscale.
- Referência viva no repo: a tabela de slots COM em
  [docs/apis/d3d11.md](../apis/d3d11.md) — os mesmos métodos que usarias
  legalmente são os que observamos no jogo. O **probe** de
  [D3D11Module.cpp](../../backends/d3d11/src/win/D3D11Module.cpp) é,
  literalmente, "a primeira aplicação D3D11 do curso" (cria device +
  swapchain escondidos).

## Camada 3 — API hooking & engenharia reversa defensiva

**Objetivo**: perceber como observadores (RenderDoc, RTSS, Steam overlay)
funcionam — e quando NÃO fazer isto.

- COM vtables e ABI estável; por que detour de corpo de função (MinHook) em
  vez de patch de slot ([ADR 0002](../architecture/adr/0002-hook-engine-and-injection.md)).
- Trampolinos, hot-patching, locking leve em hot path (contadores, não logs
  por call), loader lock e "nunca trabalhar em `DllMain`".
- Ética & fronteira técnica: anti-cheat, porque só single-player, por que é
  que o nosso loader não tenta circular filtros (SECURITY.md).
- Lab: corre `RenderLift.Loader` numa app D3D11 tua (Camada 2) e inspeciona o
  log — vês o teu frame explicado pela Research Layer.

## Camada 4 — Reconstruction & upscaling (o coração ALRR)

**Objetivo**: de 640×360 borrado a nítido e estável.

1. Filtros clássicos: bilinear → Lanczos → NIS (spatial, fallback).
2. Edge-directed: por que o plano 0.5 é "edge-first" — deteção de gradientes
   meio de `shaders/edge/`, sharpening como passo separado (`shaders/sharpen/`).
3. Temporal: porque motion vectors/history são o divisor de águas do 1.0
   (tier Temporal) e que dados o backend já captura nos ajudam (CF: o
   classificador ja identifica shadow/depth que alimentam reprojectão).
4. Métricas PSNR/SSIM/LPIPS — `docs/research/` e o simulador do CLI para
   experimentar políticas antes de hardware real.

## Mapa rápido disciplina → módulos

| Disciplina | Módulo do repo para ler primeiro |
|---|---|
| C++20 aplicado | `backends/common/src/obs/` (readers pequenos e reais) |
| Pipeline do frame | `RenderLift.CLI inspect` + `docs/research/d3d11-research-layer.md` |
| D3D11 | `backends/d3d11/src/win/D3D11Module.cpp` (probe + hooks documentados) |
| Hooking | `backends/common/src/HookEngine.cpp` + ADR 0002 |
| Upscaling | `src/reconstruction/` + `shaders/` + ADR 0001 |
| Resolução dinâmica | `src/resolution/` + perfis em `profiles/` |

> Nota de estudo: cada classe que o inspector imprime (`hdr-scene?`,
> `g-buffer?`, `backbuffer-like?`) é também um convite: pega no teu jogo
> favorito autorizado, observa-o, e compara o mapa com o que lês aqui. A
> researche começa quando a tua classificação discutir com a nossa.
