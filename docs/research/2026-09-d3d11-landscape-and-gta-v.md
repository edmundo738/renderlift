# Panorama D3D11 & GTA V — o que já existe, o que aprendemos, o que muda (2026-09-21)

> Princípio da missão: **"não descobrir por tentativa-e-erro o que já foi
> descoberto e documentado por outros."** Este documento é a base permanente
> que as próximas fases consultam antes de implementar. Fontes citadas ao
> longo; conclusiones operativas no fim.

## 1. Fontes estudadas

| Projeto | O que é | Relevância p/ RenderLift |
|---|---|---|
| [NarutoUA/gta5_fsr](https://github.com/NarutoUA/gta5_fsr) (★568) | Mod real que **substitui o upscaler nativo do GTA V por FSR 1.0** | Prova, em produção e dezenas de milhares de utilizadores, do ponto de integração "pós-pós-processamento, pré-UI" que a nossa arquitetura assume |
| [Adrian Courrèges — GTA V Graphics Study](http://www.adriancourreges.com/blog/2015/11/02/gta-v-graphics-study/) | Dissecação frame-a-frame do pipeline D3D11 do GTA V (RenderDoc) | A referência canónica do pipeline RAGE: G-buffer MRT, depth, HDR→LDR, UI |
| [ReShade](https://reshade.me) + docs (Generic Depth, DisplayDepth) | Injector de pós-processamento genérico | Lições de segurança anti-cheat; heurísticas de deteção de depth buffer; padrões de injeção |
| [rebtd7/kiero (e forks)](https://github.com/boogie77/kiero) + Rebzzel/Universal-D3D11-Hook | Tabela de métodos D3D9–12 + MinHook | Modelo exatamente equivalente ao nosso probe+vtable (chegámos à mesma técnica) |
| [GTAResources/GTAV-Research](https://github.com/GTAResources/GTAV-Research) | Fork de 2014: GTAVHook (ASI), GTAVBrowser, hash codes | Histórico; demonstra a via ASI (ScriptHook), que está fora do nosso modelo |
| [dxvk](https://github.com/doitsujin/dxvk) (referência) | Tradução D3D→Vulkan | Comprova robustez de interceptar na fronteira da API; fora do escopo prático |
| guidedhacking/GH_D3D11_Hook | Hook D3D11 mínimo educacional | Referência de bootstrapping (idiêntico ao nosso probe) |
| leaks/gtav-sourcecode-* | Guias de build de código RAGE vazado | **Fora de escopo por lei/ética** — este projeto MIT estuda apenas introspeção pública/docs |

## 2. As grandes descobertas que mudam as nossas prioridades

### 2.1 O GTA V já tem DRS-manual nativo: "Frame Scaling Mode"
`gta5_fsr` não torna o jogo mais barato do zero — ele **só funciona com
Advanced Graphics → Frame scaling mode < 1.0** (0.5×/0.667×/0.75×/0.883×).
Ou seja, o RAGE **já renderiza a cena 3D a resolução interna reduzida** e
depois corre um pass de upscale. O que o mod faz é substituir esse pass.

Consequência para o nosso roadmap em GTA V:

- **"Steer" (0.3) em GTA V pode ser zero-hook de render targets**: a ladder
  interna já existe no engine. Manipular Frame Scaling via
  `commandline.txt`/settings e hooks de controlo (a investigar na 0.3) é
  muito menos invasivo do que redirecionar CreateTexture2D/OM no RAGE.
- **Reconstruct (0.4) = substituir o pass de upscale RAGE por ALRR** —
  exatamente o ponto onde FSR 1.0 foi rebitado no lugar do upscaler nativo
  (config de `gta5_fsr.ini`, chain proxy d3d11.dll).
- **DRS dinâmico (1.0) é nosso**: o Frame Scaling do RAGE é estático,
  escolhido em menu; mover frame-a-frame é a nossa contribuição (áspera:
  mudar a resolução interna em runtime sem ResizeBuffers completo — o pass
  de upscale tem de ler a resol interna atual a cada frame; o hook do
  upscale é o sitio natural para a injetarmos).

⚠️ Risco técnico anotado: como é que o RAGE decide a resolução interna
derivada do Frame Scaling (config reload, ResizeBuffers, flag em memória)?
A resposta sai da **0.2 observe real** — a textura de upscale final muda de
tamanho com Frame Scaling alterado em jogo? Observar.

### 2.2 O método de injeção preferido da comunidade GTA V: **proxy `d3d11.dll`**
gta5_fsr, ENB e (variantes de) ReShade não são "injetados": copiam-se para a
pasta do jogo como `d3d11.dll`/`dxgi.dll` e o **carregador do Windows** dá a
DLL ao jogo (search order); o mod re-encaminha exports para a DLL real e
anexa a lógica. Efeitos laterais documentados:

- **Compatível com anti-cheat** na medida em que não escreve memória nem
  cria threads remotas; o próprio autor do gta5_fsr corre no GTA Online
  não-intervalos. (ReShade assinado desativa add-ons online quando deteta
  jogo MP — a mesma ética do nosso SECURITY.md).
- Suporta **cadeia de proxies** (`ProxyLibrary=` no ini) — se ENB co-existir,
  renomeia-se e coordena-se. O nosso modelo deve herdar este padrão para
  GTA V: `RenderLift.d3d11.dll` como **proxy estável** fica no roadmap (fase
  D/0.4), mantendo o `RenderLift.Loader` (injeção ativa) para labs R&D.
- Compara-se com a nossa abordagem loader/remotethread (RenderLift.Loader):
  superior para *descoberta* (não toca nos ficheiros do jogo, ativa a
  quente, controla uninstall), inferior a *diário* por ser mais visível.
  **Conclusão: as duas existirão**; loader para research, proxy para uso.

### 2.3 O pipeline D3D11 documentado do GTA V (Courrèges)
Dissecação frame-a-frame (parte 1–3) — o "mapa" que importa ao classificador:

1. **CSM sombras** (~1000 draws, 4 sombras frustum-cull) → RT shadow com
   downscale 1/8 para blur cheap ("early-out"). → nossa classe
   `shadow-map?` + `downsample?` validadas.
2. **G-Buffer via MRT: 5 targets + depth/stencil (~1900 draws, front-to-back)**
   Diffuse/Normal/Specular/Irradiance (RGBA8) + profundidade D24S8 log-reversed,
   stencil com IDs por categoria (0x89 jogador, 0x82 veículo, 0x01 NPC).
   → `g-buffer?` (MRT, RT|SRV, display-sized) bate certo.
3. **Combinação dos G-Buffers → HDR** (`hdr-scene?`) → SSS de pele via
   stencil; pós-processamento chain; DOF, motion blur, bloom (Kawase loops),
   lens flares; UI overlay; **tonemapping → LDR fino** → pass de upscale e
   present. → cadeias `ldr-post?` + `backbuffer-like?`.

Nota fraternal: o estudo de **MGS V** (mesmo autor) mostra **upscale bilateral
consciente de profundidade** (half-res GI → full com edge crisp via depth).
É evidência publicada da nossa decisão "ALRR Edge usa depth-aware upscale
antes de irmos temporal" — e fornece o conceito para o tier 0.5.

### 2.4 ReShade — regras anti-cheat & stencil/depth heurísticos
- Builds assinadas **limitam add-ons** e só deixam os hooks mínimos em SP;
  ao detetar jogo online, até o acesso ao depth desaparece (wallhack). →
  a nossa política single-player-only não é exagero — é o padrão da indústria.
- **Generic Depth** (seleção automática do depth buffer): aspect ratio vs
  viewport, contagem de draw calls, "copy before clear", override manual.
  DisplayDepth/ShowDepth para validação visual. → a nossa regra `depth`
  (formato de depth + display-size) já é equivalente; para `steer`, quando
  precisarmos do depth real de cena, adicionar as mesmas heurísticas
  (aspect ≥ alvo, draw count máximo).
- ReShade lazy-init em vários presents; swapchain re-create re-inicializa.
  → o nosso módulo reinicia estado em `ResizeBuffers` pela mesma razão (já
  canonizado no ADR 0002).

### 2.5 kiero / GH_D3D11_Hook — chegámos à mesma engenharia
kiero tem uma METHODS TABLE (vtable entries pré-calculadas por versão+API)
+ MinHook MultiHook; os exemplos (ImGui overlay em GTA V etc.) são exatamente
o padrão do nosso probe `D3D11CreateDeviceAndSwapChain` + anchors globais das
vtables. Berengel: **o nosso hook engine está ao nível do que produção usa**.
Diferenças nossas deliberadas: (a) leitura read-only das vtables (0 slots
manipulados — sem crashes por .rdata); (b) observação-first e logs estruturados
RLCAP1 (kiero/ImGui não observam, assumem); (c) contrato externo
`RenderLiftInstall/Uninstall` (loader-agnóstico).

### 2.6 O que *não* usar do nosso caminho: hack de source leak
Guias de build do código RAGE vazado existem (Vasik96 et al). **Fora do
escopo** — licenciamento, MIt do repo, SECURITY.md. Para perguntas de
"como o engine escolhe a resolução interna?", a via é introspecção
autorizada (a nossa própria 0.2 observe) + documentação pública dos
Registered settings (`commandline.txt`, settings.xml não modificáveis
programaticamente in-game — a verificar).

## 3. Comparação direta — RenderLift vs existentes

| Capacidade | gta5_fsr | ReShade | kiero/ImGui | **RenderLift (0.2)** |
|---|---|---|---|---|
| Fundamentação da escolha de recursos | hard-coded (só GTA V) | heurísticas genéricas | nenhuma | **classificador com confiança+razão** (genérico) |
| Interseção do pipeline | pass de upscale (proxy dll) | backbuffer/pos | vtable hooks (MinHook) | device+context+swapchain, vtable anchors |
| Dinâmica de resolução | estática (menu) | n/a | n/a | ladder + controlador DRS (spec 0.1), runtime 1.0 |
| UI nativa preservada | sim (present depois) | sim | n/a | sim — `uiNative`, backbuffer-like intocável |
| Anti-cheat/consciência online | defensivo (no memory writes) | hard rule | n/a | single-player-only (SECURITY.md) |
| Injeção | proxy `d3d11.dll` | proxy | manualCreateThread/tchar loader/payload | Loader dedicado (research); proxy-planeado |
| Multi-jogo por design | não | sim | sim | sim — ALRR Core API-agnóstico + backends |
| Depth-aware upscale | não (ou não usa) | opcional em add-ons | n/a | planejado tier Edge 0.5 (MGS V study referencia) |
| História/multi-frame | não | não | n/a | planejado tier Temporal 1.0 |

## 4. O que falta validar dentro do GTA V real (checklist da 0.2 observe)

1. O maior viewport bate 1366×768 e coincide com a textura `backbuffer-like?`
2. A cadeia `hdr-scene?` existe exatamente como Courrèges descreve (FP16
   display-sized, RT|SRV) e está entre MRT G-buffer e LDR post?
3. Existe um **pass de upscale nativo** observável ao variar Frame Scaling
   (muda o tamanho de uma textura final LDR? identificar **qual** id de
   textura muda) → candidata ao pass ALRR.
4. Desenho do UI: confirmar que o desenho de UI aterrisa co
   `backbuffer-like?` em full res (evidencia pelos draws + bound após
   tonemapping).
5. Overhead: com cap 600 frames, overhead de timings Present→Present
   documentado (e atenuado pelos contadores/hot path rules).
6. Sem crash/sem invisível por SET/RESET de graphics options (MSAA on/off,
   Fullscreen↔Windowed: ResizeBuffers lines).

## 5. Conclusões operativas (mudança de plano)

- **0.2 observe VALIDA o plano de pipeline do GTA V** — nada altera antes.
- **0.3 steer muda de abordagem em GTA V**: em vez de redirecionar alvos RT,
  prioriza-se (a) validar/alterar Frame Scaling nativo; (b) se entrar DRS
  dinâmico, incorpora-se no pass de upscale (lugar do gta5_fsr). A
  arquitetura `SteeringPolicy`/classifier mantém-se — é o cérebro que
  *decide* o rung e confirma qual é o pass.
- **Aliás — 0.3 também começará _proxy dll_** (modo diário) mantendo Loader
  para research — herdança do modelo gta5_fsr/ENB/ReShade.
- **ALRR Edge (0.5)**: spec de bilateral/edge-aware upscale com depth já
  tem literatura (MGS V study; ReShade BilateralUpscale-like effects).
- **Anti-cheat**: single-player-only fica; documentar no README "não usar em
  GTA Online" — lógica ReShade-like.

## 6. Links canónicos (pin-ear marcador)

- Courrèges, "GTA V Graphics Study" — partes 1/2/3 (G-buffer, LOD/Reflexos, Post-Effects).
- NarutoUA/gta5_fsr — repo + `gta5_fsr.ini` (Frame Scaling, ProxyLibrary, AutoSharpness).
- reshade.me releases 5.0 e docs Generic Depth/DisplayDepth.
- boogie77/kiero + Rebzzel/Universal-D3D11-Hook + guidedhacking/GH_D3D11_Hook.
- GTAResources/GTAV-Research (histórico, 2014).
- doitsujin/dxvk (tradutor D3D→VK; referência de robustez de fronteira).
