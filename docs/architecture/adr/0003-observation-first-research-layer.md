# ADR 0003: Integration é Observation-First — a Regra de Ouro

- **Status**: Aceite
- **Data**: 2026-09-21
- **Decidido por**: projeto (revisão externa adotada como lei do projeto)

## Contexto

RenderLift, por natureza, reescreve como um jogo renderiza. A forma clássica
de falhar com este tipo de software é chegar cedo demais a "alterar": escrever
o steering de render targets traçando-nos a um PDF da arquitetura do engine e
descobrir no jogo real que os 30 render targets do frame não são o que
pensávamos. O risco é dobrado pelo público-alvo (PCs fracos, baixa margem de
erro) e pelo primeiro lab (GTA V: pipeline deferred complexo, MRT, MSAA HQ,
cadeia HDR→LDR, UI separada).

Uma revisão externa do plano de integração propôs inverter a ordem: construir
capacidade de **observação total antes de tocar em um pixel**. A proposta foi
adotada como **regra de ouro do projeto**:

> **Nunca alterar o rendering de um jogo sem antes conseguir observá-lo.**
>
> Primeira integração real é com resolução FIXA e DRS DESLIGADO.

## Decisão

1. **Fases de integração são um feature do produto, não um hábito de
   desenvolvimento.** São expostas como `integration.mode` no perfil do jogo:

   | Fase | `mode`          | Pode alterar o rendering? | Entrega |
   |------|-----------------|---------------------------|---------|
   | A    | `observe`       | **Não** (log apenas)      | 0.2     |
   | B    | `observe`+class | Não (classifica offline)  | 0.2     |
   | C    | `steer`         | Uma coisa: res interna fixa 640×360, **DRS off** | 0.3 |
   | D    | `reconstruct`   | + ALRR reconstruição      | 0.4–0.5 |
   | E    | `full`          | + DRS controller ativo    | 1.0     |

   A ordem é a versão operacional da máxima recebida:

   > **Ver → Medir → Classificar → Alterar uma coisa → Reconstruir → Otimizar.**

2. **`dynamicResolution` permanece no schema, mas é INERTE até `full`.**
   Configuração sem efeito é uma mentira; inerte declarado no perfil e no
   validador é uma decisão honesta.

3. **O Research Layer é código do produto em modo `observe`**, não tooling
   separado:
   - formato de captura `RLCAP1` (linhas legíveis por humanos e parseáveis) —
     `backends/common/src/obs/CaptureFormat.cpp`;
   - observação no backend D3D11: hooks em `CreateTexture2D`,
     `Create(RenderTarget|DepthStencil)View`, `OMSetRenderTargets`,
     `RSSetViewports`, `Draw*` (agregado por frame), `Present`;
   - classificador offline de recursos com confiança + razão em linguagem
     natural — `ResourceClassifier.cpp`;
   - Frame Resource Inspector offline: `RenderLift.CLI inspect <capture.log>`.
   O humano confirma ou corrige a classificação — a heurística nunca liga
   steering sozinha.

4. **Critério de saída da 0.2** (portão para `steer`):
   - dentro do GTA V corremos `observe` por ≥600 frames sem crashes nem
     salto de CPU perceptível (overhead documentado);
   - o inspector produz um mapa de recursos onde a cadeia 3D (HDR scene),
     G-buffer (MRT), profundidade principal e o destino do UI (backbuffer-like)
     são identificados e confirmados por um humano;
   - ADR 0004 confirma o conjunto a steer e o limite fase-C (640×360 fixo).

## Consequências

- Perfil GTA V nasce com `"integration": {"mode": "observe", "observationFrames": 600}`.
- A regra aplica-se a TODOS os backends futuros (D3D9/10/12, Vulkan): cada um
  começa com a sua camada de observação — é o item 1 da checklist em
  `backends/README.md`.
- Bug encontrado durante observação do nosso próprio código (ex.: views
  desconhecidas em conjuntos MRT) é tratado com honestidade: o modelo conta
  as views que o GPU tinha ativas mesmo quando não as podemos resolver.
- `RenderLift.Loader` é a utilidade de injeção dos labs autorizados; single-
  player apenas (ver SECURITY.md).

## Relação com outras decisões

- [0001 — RenderLift é o produto, ALRR é o motor](0001-renderlift-product-alrr-engine.md):
  0003 define COMO o motor entra no jogo sem o partir.
- [0002 — hook engine](0002-hook-engine-and-injection.md): os detours
  observation-only deste ADR correm sobre o motor lá decidido.

## Referências

- Revisão externa do plano RenderLift (2026-09-21, paste do eduardo) — origem
  da estratégia observation-first; adotada como regra de ouro.
- GTA V pipeline deferred: partes 2–6 das anotações do Adrian Court / github
  "vgv" gtagfx dumps públicos — base das heurísticas do classificador.
