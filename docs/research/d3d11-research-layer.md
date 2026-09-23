# D3D11 Research Layer — como observamos um jogo (0.2)

**Lei**: nunca alterar o rendering sem antes conseguir observá-lo
([ADR 0003](../architecture/adr/0003-observation-first-research-layer.md)).

Este documento é o manual da Research Layer do backend D3D11: o que é
observado, onde é gravado, como ler o resultado e quais são os limites
conhecidos. Tudo aqui corre em `integration.mode = observe` e **não toca em
nenhum parâmetro de rendering** — cada hook é um passthrough medido.

## 1. O que é capturado

| Superfície (vtable slot)                       | Evento RLCAP1   | Frequência   | Notas |
|------------------------------------------------|-----------------|--------------|-------|
| `ID3D11Device::CreateTexture2D` (5)            | `tex`           | por criação  | dims, formato DXGI, bind flags, MSAA |
| `ID3D11Device::CreateRenderTargetView` (9)     | `rtv`           | por criação  | liga view → textura |
| `ID3D11Device::CreateDepthStencilView` (10)    | `dsv`           | por criação  | liga view → textura |
| `ID3D11DeviceContext::OMSetRenderTargets` (33) | `rt`            | por chamada  | conjunto RTV(+DSV) ativos — detecta MRT |
| `ID3D11DeviceContext::RSSetViewports` (44)     | `vp`            | por chamada  | 1º viewport: estimativa do display |
| `ID3D11DeviceContext::Draw/DrawIndexed` (13/12)| (contadores)    | NUNCA por chamada | hooks contam; agregado sai em `drawstat` |
| `IDXGISwapChain::Present` (8)                  | `drawstat`+`present` | por frame | fecha o frame; bypass total se `frame == cap` |
| `IDXGISwapChain::ResizeBuffers` (13)           | linha `resize`  | por evento   | fullscreen↔windowed |

Mapa de slots completo: [d3d11.md](../apis/d3d11.md).

## 2. Como ligar (lab autorizado, single-player — SECURITY.md)

```text
# dentro do jogo (GTA V), numa shell ao lado:
set RENDERLIFT_LOG=D:\labs\gtav-frame1.log      :: destino do capture
set RENDERLIFT_OBSERVE_FRAMES=600               :: já é o default (0 = sem cap)
RenderLift.Loader.exe --exe GTA5.exe --dll RenderLift.D3D11.dll
```

O loader injeta a DLL (`LoadLibraryW` remoto) e chama `RenderLiftInstall`.
Start/end do módulo ficam marcados (`RLCAP1 hello …`, `RLCAP1 bye …`), e ao
atingir o cap a camada desliga-se a si própria (`RLCAP1 cap …`) para deixar o
jogo em passthrough ~grátis.

## 3. Como ler

```text
RenderLift.CLI inspect D:\labs\gtav-frame1.log
```

O inspector (funciona em qualquer OS, o log é só texto):

1. faz parse estrito das linhas RLCAP1;
2. agrega por frame e por recurso (quem é bound ao quê, quantas vezes, MRT?);
3. estima a resolução do display pelo maior viewport;
4. classifica cada textura com confiança + razão legível;
5. propõe candidatos para `steer` (fase C) — HDR/G-buffer/LDR post = 3D,
   backbuffer-like = intocável (é a casa do UI nativo).

Exemplo com um capture simulado da estrutura documentada do GTA V:

```text
RenderLift.CLI inspect docs/research/samples/gta-v-simulated.rlcap
```

Saída esperada: 1 HDR scene (R16G16B16A16_FLOAT @ display), 3 G-buffers
(R8G8B8A8_UNORM em MRT de 3), 1 depth principal, 1 downsample (683×384),
1 shadow atlas (1024×1024) e 1 backbuffer-like (RT sem SRV). Compare com as
partes 2–6 das anotações públicas do pipeline do GTA V — o classificador
chega sozinho ao mesmo mapa.

## 4. Formato RLCAP1

Linha única, tokens `chave=valor` separados por espaços, prefixo `RLCAP1`:

```text
RLCAP1 tex dev=0xADDR id=0xADDR w=1366 h=768 fmt=28 bind=0x28 mips=1 array=1 samples=1
RLCAP1 rtv dev=0xADDR id=0xVADDR res=0xADDR
RLCAP1 rt  ctx=0xADDR n=3 rtvs=0xV1,0xV2,0xV3 dsv=0xV4
RLCAP1 vp  ctx=0xADDR n=1 x=0 y=0 w=1366 h=768
RLCAP1 drawstat frame=129 calls=1487 maxidx=24576 maxvtx=3072
RLCAP1 present sc=0xADDR frame=129
```

Ponteiros são **tokens de identidade** — só para juntar recursos e views
entre eventos, nunca para aceder offline. `fmt` é o número DXGI_FORMAT
(28=R8G8B8A8_UNORM, 10=R16G16B16A16_FLOAT, 45=D24_UNORM_S8_UINT, …);
`bind` é o bitmask D3D11_BIND_* (SRV=0x08, RT=0x20, DS=0x40, UAV=0x80).
Linhas mal-formadas são saltadas (o inspector conta-as como `skipped`).

## 5. Overhead e riscos conhecidos

| Risco / custo | Mitigação implementada |
|---|---|
| `Draw*` é o caminho mais quente do engine | só contadores atômicos sob mutex do módulo; zero I/O na hot path |
| 1,5k lines/frame se logássemos cada draw | agregado `drawstat` 1×/frame |
| thrash de file I/O dentro de Present | commits em buffer, flush a cada 128 eventos / 30 frames / cap |
| topo de log em cada pass | observação desliga-se no cap (`RLCAP1 cap …`) |
| probe device WARP em PCs sem GPU adequada | probe tenta hardware → fallback WARP; usa apenas endereços de vtable |
| múltiplos devices/contexts (overlay, editor) | toda a captura carrega o ponteiro `dev`/`ctx` — o inspector distingue |

⚠️ **Ainda não verificado**: primeira corrida real no GTA V (entrega 0.2
final). O loader e os hooks são vetted por code review + compilados no CI
Windows, mas o progresso "dentro do jogo" é o próximo marco do projeto — o
sucesso é medido pelo mapa de recursos produzido, não por commits.

## 5.1 Primeira corrida real (GTA V Legacy 1.0.3889.0, 2026-09-21)

**Run 1 — falha do lado do loader.** `LoadLibraryExW(LOAD_LIBRARY_AS_DATAFILE)`
+ `GetProcAddress` devolvia NULL para uma DLL com a export table intacta →
`error: export 'RenderLiftInstall' not found`. Correção: o loader passou a ler
diretamente o PE Export Directory do ficheiro (`findExportRva`) e calcular
`remote VA = base remota + RVA` (commit `b2bf495`), com verificação CI do
contrato de exports + evidência publicada (`RenderLift.D3D11.exports.txt`).

**Run 2 — `0xc0000005` no primeiro contacto.** LoadLibrary remoto OK
(base `0xBA1E0000`), export resolvido (`RVA 0x4BA0`), thread remota criada —
e a thread morreu com Access Violation, derrubando o processo. WER Event
1000: `Fault offset == 0xBA1E4BA0`, **bit-exato com a entry VA de
`RenderLiftInstall`**; zero linhas de log sobreviveram. Disassembly da DLL
que crashou (objdump, sha256 `eebf2c46…`) mostrou o entry real: store na
stack na 1ª instrução → /GS cookie → `state()` **magic static** →
`_Init_thread_header` (CRT) → construção implícita de `ModuleState`
(`std::mutex`/`std::ofstream`/vtables) → MinHook → e o logger só no fim
desta fila, com path relativo para o cwd do jogo (pasta OneDrive).
Eliminados pela própria disassembly: D3D11/Device/Context/SwapChain/vtable-
probe como causa do crash de entrada (correm muito depois); eliminada ABI
(x64 tem convenção única; o `GetExitCodeThread` devolveu o código — o
contrato de retorno funcionou).

**Correção (commit `8cbb7cb`) — install "evidence-first":**

- Exports com a forma exata de thread-proc: `HRESULT WINAPI fn(LPVOID)`
  (nomes do contrato inalterados);
- `RLCAP1 install cp=N` escrito com `fopen/fputs/fclose` puro **antes** de
  qualquer STL/global/lock — a partir do `cp=1` (primeira instrução
  efetiva): `1` entrou → `2` estado (`new ModuleState()` explícito, sem
  magic static) → `3` log → `4` MinHook → `5`+`50–55` vtables (register-
  class/window/probe HW/WARP/leitura/cleanup) → `60+i` por hook → `70`
  enable → `80` ARMED;
- log **ao lado da DLL** (nunca na pasta do jogo), fallback `%TEMP%`,
  `RENDERLIFT_LOG` continua a mandar;
- guarda SEH em toda a instalação: o filtro regista
  `RLCAP1 seh phase=N code=0xXXXXXXXX addr=%p` (ExceptionAddress exato) e
  devolve `0xE<fase><código>`; falhas normais `0x8000A001…A030` por passo.
  Diagnóstico, não mascaramento: sucesso continua a significar 9 hooks
  armados.

**Run 3 (v3) — mesmo resultado, e a lição de protocolo que ela ensina.**
`remote RenderLiftInstall returned 0xc0000005`, **nenhum log criado, nenhum
`cp=1`**. Revisão crítica do próprio protocolo: (a) `cp=1` corria **fora**
do `__try`, e (b) a sua cadeia ainda era CRT-pesada (`vsnprintf` +
`fopen_s`/`fputs`/`fclose` — stdio aloca, bloqueia e inicializa locale).
Um AV dentro dessa cadeia produz *exatamente* "0xc0000005 sem cp=1 e sem
ficheiro" — ou seja, "cp=1 ausente" **não** provava falha pré-DLL. Protocolo
corrigido (v3.1):

- **toda a entrada observável dentro do SEH** — marca, `cp=1`, `installSteps`;
- **transporte de evidência kernel32-only**: `CreateFileA`/`WriteFile`/
  `CloseHandle` + leitura de env no PEB; zero CRT stdio, zero heap, zero
  locks — formatador de strings próprio (`%d/%u/%ld/%lu/%zu/%x/%p/%s`);
- **marca binária de entrada primeiro**: `RenderLift.entry` (32 bytes,
  `CREATE_ALWAYS`, magic `RLENT01`, pid/tid/tick/nº da tentativa) — a sua
  existência+mtime responde binariamente a "a execução chegou ao primeiro
  byte observável?", independentemente de texto;
- **dual-sink**: cada linha vai para a pasta da DLL *e* para `%TEMP%`
  (duas cópias — uma ACL problemática nunca mais fica com a única prova);
- exports inalterados: `HRESULT WINAPI fn(LPVOID)`, mesmos 3 nomes;
  OBSERVE-only, zero steering.

**Run 4 (v3.1) — caso B da matriz, com dupla testemunha.**
`returned 0xc0000005`; **nenhuma** `RenderLift.entry` (nem pasta-lab, nem
`%TEMP%`), **nenhum** log em nenhum sink. Leitura restrita e honesta: nenhuma
evidência sobreviveu antes do AV — o que ainda não é "pré-DLL provado", é a
fronteira seguinte a isolar.

**Experimento v3.2 (fronteira CreateRemoteThread → remote entry):**

- export nova `RenderLiftEntryProbe`: stub mínimo — sem CRT/STL/globals de
  escrita/D3D11/MinHook/SEH; só kernel32 via IAT + literal `.rdata` + o
  LPVOID param; escreve `RLPROBE1 ok` no ficheiro dado por
  `--param <path>` (tornado remoto via VirtualAllocEx+WriteProcessMemory) e
  retorna sempre `0x12345678`;
- o loader, antes de **qualquer** chamada remota, faz `VirtualQueryEx` na
  página do entry (State/Type/Protect/AllocationBase/RegionSize/guard) e
  **aborta a chamada** se a página não for executável — diagnóstico sem
  crash; e despeja as mitigações do processo (`GetProcessMitigationPolicy`:
  DEP/ASLR/CFG/ACG/Signature/ImageLoad/ExtensionPoint);
**Run 5 (probe v3.2) — a proteção disparou e apanhou a CAUSA-RAIZ de
todos os crashes anteriores.** O loader imprimiu, *antes* de qualquer
chamada remota:

```
RenderLift.D3D11.dll loaded remotely at 0x0000000010930000
export RenderLiftEntryProbe at RVA=0x00005ed0, remote VA=0x0000000010935ED0
entry page: state=FREE type=? protect=NOACCESS => NOT EXECUTABLE
→ chamada abortada (diagnóstico sem crash)
```

A "base" era perfeitamente 32-bit, mas o param remoto fora alocado em
`0x26D5DD20000` (>4 GB): `LoadLibraryW` devolve **HMODULE de 64 bits** e
`GetExitCodeThread` lê apenas um **DWORD de 32 bits** — o loader estava a
usar a **metade baixa truncada** da base real. RVA certo + base errada =
endereço não mapeado. Isto explica deterministicamente TODA a saga
(v2/v3/v3.1): `CreateRemoteThread` para VA não mapeada → instruction-fetch
`0xC0000005` imediato → WER "unknown module" com fault offset absoluto ==
a VA errada → zero evidência (o nosso código **nunca chegou a executar** em
nenhuma corrida — D3D11/MinHook/ABI eram inocentes downstream de um bug do
loader). Correção: depois do exit code != 0, a base real é resolvida por
`EnumProcessModulesEx(LIST_MODULES_ALL)` + `GetModuleBaseNameW` (HMODULE de
64 bits genuíno), com o exit code impresso apenas como prova de sucesso; e
  o `VirtualQueryEx` do entry permanece como guarda final antes de chamar.

  Regra geral do probe (mantém-se): probe marca + `0x12345678` → fronteira
  sã; probe também `0xC0000005` sem ficheiro → a falha é de entrega da
  thread remota e não se regressa a D3D11 até a resolver.

**Run 6 (lab pack v3.3) — cadeia completa PASS: entrada → probe → vtables →
hooks → Present; draw path em aberto.** Com a base real de 64 bits, o probe
executou in-process e devolveu `0x12345678` (`RenderLift.probe` criado), o
install armou **9/9** hooks e `Present_Hook` disparou todos os frames até
`RLCAP1 cap frames=600`. Mas **TODOS** os contadores de contexto ficaram a
zero (`drawstat calls=0 maxidx=0 maxvtx=0`, zero eventos `rt`/`vp`) — o
jogo estava sem foco/janela fechada durante grande parte da janela.
Conclusão registada: `Present path = PROVEN` ≠ `draw path = PROVEN`; um
Install com HRESULT 0 e Present observado **não** provam o caminho de draw.

### 5.2 Checkpoint 0.2-final: prova empírica do draw path (v3.4)

Pergunta aberta: *por que `calls=0` com Present vivo?* Hipóteses mantidas
propositadamente separadas — a v3.4 foi desenhada para **distingui-las**,
não para declarar nenhuma antes da evidência:

- **H1 — cobertura insuficiente de slots**: só os slots 12/13 (`DrawIndexed`,
  `Draw`) alimentavam o drawstat; uma engine moderna (RAGE) renderiza quase
  tudo via `DrawIndexedInstanced`(20)/`DrawInstanced`(21)/`DrawAuto`(38)/
  indirect(39/40), nenhum dos quais estava hookado.
- **H2 — caminho de contexto diferente**: os hooks ancoram nas funções do
  **immediate context** do nosso probe; se o jogo renderizar num contexto de
  classe/implementação diferente (p.ex. maioritariamente *deferred contexts*,
  ou modo DX10/DX10.1 do GTA V — o swapchain `Present` é DXGI comum e
  dispararia na mesma), o nosso código nunca entra no caminho.
- **H3 — contaminação de estado**: janela de 600 frames com o GTA sem
  foco/em pausa/menu → pouco ou nenhum rendering de cena gera draws.

Arquitetura da prova (OBSERVE-only; loader congelado; alterações apenas na
DLL — zero efeito visual, zero steering, zero redirect/shader/resolução):

- **14 hooks** (9 anteriores + DrawIndexedInstanced(20), DrawInstanced(21),
  DrawAuto(38), DrawIndexedInstancedIndirect(39), DrawInstancedIndirect(40));
- **first-fire** obrigatório por slot: `RLCAP1 first slot=<nome> ctx=0x…`
  (primeira ocorrência de cada um dos 9 hooks de contexto);
- **contextos distintos**: `RLCAP1 context first=0x…`/`context new=0x…`
  (conjunto limitado de 32; overflow contado, sem log por chamada);
- por frame: `RLCAP1 draws frame=N d= di= diinst= dinst= dauto= diind=
  dinstind= om= vp=` (linha raw — o `drawstat`/wire-format legado fica
  **intocado** e o inspector offline ignora tags desconhecidas);
- **resumo da janela** no cap: `RLCAP1 summary frames=N … draws=T ctxs=C
  ctxovf=O verdict=DRAWPATH_ACTIVE|DRAWPATH_ZERO` antes do terminador
  legado `RLCAP1 cap frames=N`;
- **janela de 2000 frames** por omissão (`RENDERLIFT_OBSERVE_FRAMES`
  continua a sobrepor — lido do ambiente do processo-alvo; definir antes de
  lançar o jogo);
- **re-arm**: nova chamada remota a `RenderLiftInstall` com `installed==true`
  não é mais no-op — repõe frame/contadores/primeiros-disparos/contextos,
  re-lê o cap e reabre a janela: `RLCAP1 rearm cap=2000` (hooks ficam
  instalados; DLL nunca é recarregada; loader intocado).

Árvore de decisão pós-teste (jogo **focado + gameplay ativo** durante toda
a janela — H3 eliminada por protocolo):

| Observação | Conclusão |
|---|---|
| Algum slot novo dispara (`first slot=DrawIndexedInstanced…`), contadores >0 | **H1 confirmada** — o jogo passa pelo immediate context nesses slots; draw path PROVEN |
| Só 12/13 disparam | draw path PROVEN com cobertura antiga bastante |
| Draws a zero mas `om=`/`vp=` >0 no mesmo ctx | immediate context usado sem draws nesses slots → procurar outros slots/indirect (re-analisar) |
| TODOS os hooks de contexto a zero com jogo focado a renderizar | **H2** — o caminho de render não usa as funções ancoradas (deferred-domínio ou modo não-D3D11); `ctxs=0` no summary corrobora |
| Present para durante a janela | contaminação de protocolo (jogo parou de apresentar) — repetir com foco garantido |

**RESULTADO (teste do utilizador, lab pack v3.4, GTA V Legacy, pid 43820,
gameplay contínuo):** janela completa executada —
`RLCAP1 summary frames=2000 d=0 di=0 diinst=0 dinst=0 dauto=0 diind=0
dinstind=0 om=0 vp=0 draws=0 ctxs=0 ctxovf=0 verdict=DRAWPATH_ZERO`,
terminador `cap frames=2000`, **zero** linhas `first slot=`, **zero**
linhas `context first=/new=`. Present disparou os 2000/2000 frames
(Checkpoints A/B PROVEN). Interpretação registada: **H1 enfraquecida, não
refutada** (cobertura alargada a 7 famílias de draws e nada disparou); o
que NÃO se pode concluir: "o GTA não faz draws" / "o probe está
definitivamente errado" — a única coisa provada é que as funções ancoradas
pelo probe não receberam essas chamadas na janela. Próxima questão:
**qual é o Device/Context REAL do GTA e que endereços de método utiliza?**

### 5.3 Checkpoint H2 — objeto real vs probe (v3.5, diagnóstico puro)

Desenho (decisão do utilizador; ordem A das três arquiteturas de
referência): no **primeiro Present** (o swapchain real provado), caminhar

```
REAL IDXGISwapChain → GetDevice(IID_ID3D11Device) → GetImmediateContext()
→ GetType()/GetFeatureLevel() → vtables reais → endereços por slot
```

e comparar palavra a palavra com os valores resolvidos pelo probe:
`RLCAP1 probe device=… ctxvt=…` (install), `RLCAP1 real swap=… device=…
context=… ctxvt=… type=… fl=…` (1.º present), **15 linhas
`RLCAP1 cmp name=… slot=… probe=0x… real=0x… SAME|DIFFERENT`** (inclui
`Present` como controlo positivo embutido — esse hook dispara, logo a sua
linha DEVE dizer SAME — e `ExecuteCommandList`(58) para a pista H3),
veredicto agregado `RLCAP1 real verdict=PROBE_EQ_REAL | PROBE_NE_REAL
diffs=N | NO_ID3D11DEVICE | SEH_FAIL`. A caminhada é protegida por SEH
próprio (uma falha nossa nos diagnósticos nunca pode derrubar o thread de
render do jogo; a falha vira evidência, não crash). **Zero hooks novos
(14 → 14)**, wire-format e contadores intactos, `DRAWPATH_ZERO` não é
mascarado. Foco passa a ser **variável medida**: `RLCAP1 focus state=…` +
`focus transition=A->B frame=N` (foreground-window PID == pid do jogo) —
nunca declarado como causa. Referências externas consideradas:
gta5-extended-video-export (mesma cadeia swapchain→GetDevice→GetImmediate
Context a partir do Present — ordem A); GTA5_FSR (wrapping na criação —
ordem B, só se A não bastar); GameHook/gamehook_gtav (pipeline profundo —
muito mais tarde). Leitura: PROBE_NE_REAL → H2 confirmada, próximo passo é
hook mínimo (só Draw/DrawIndexed) nos endereços REAIS; PROBE_EQ_REAL com
draws a zero → H2 perde força, H3 (deferred/command-list) sobe (observar
ExecuteCommandList / criação de deferred contexts); NO_ID3D11DEVICE →
modo não-D3D11 (DX10/DX10.1) — resposta direta ao DRAWPATH_ZERO.
**Resultado (selado; GTA V Legacy 1.0.3889.0, pid 36752, janela inteira
2000/2000): `verdict=PROBE_NE_REAL rows=15 diffs=8`.** O device real é o
mesmo do probe (`devvt` IGUAL; CreateTexture2D/RTV/DSV SAME), mas o
**immediate context real é uma classe RAGE residente em heap**
(`context=0x2501a534128` com `ctxvt=0x2501a534130`, heap — não .rdata de
módulo; o ctx do probe vivia noutra região), `type=IMMEDIATE(0)`. Os 7
slots da família draw DIFFEREM (DrawIndexed/Draw/…Instanced×2/DrawAuto/
…Indirect×2 — todos os alvos REais no vizinho `0x7fff16cxxxx/16dxxxx`);
OMSetRenderTargets/RSSetViewports/ExecuteCommandList/ResizeBuffers SÃO
IGUAIS. Foco medido: `UNFOCUSED` a janela toda (métrica por PID; protocolo
da corrida deixava o PowerShell visível — dado, nunca causa declarada).

**Controlo permanente anti-determinismo-de-vtable (a "paradoxo do
Present"):** o slot 8 do swapchain REAL também DIFFERE do probe
(`…18159530 → 0x7ffe4e207510`) **e no entanto o hook de Present do probe
capturou 2000/2000 frames**. Logo: `endereço probe ≠ endereço real` NÃO
implica falha de hook — quem chama resolve o entry point na instância real,
e o padrão de chamada do jogo torna DIFFERENT não-decisivo em geral. H2 está
PROVADA como *existência da diferença*; H2 como *causa* do DRAWPATH_ZERO
**não** está provada. E "renderer deferred ≠ deferred context":
`type=IMMEDIATE(0)` descreve apenas aquele contexto imediato — sem evidência
CreateDeferredContext→FinishCommandList→ExecuteCommandList não se afirma
nada sobre contextos diferidos.

### 5.4 Microtest v3.6 — caminho de draw REAL (2 hooks nos endereços reais)

Pergunta binária: **as funções para que a vtable do contexto REAL aponta
(slots 12 DrawIndexed, 13 Draw — lidos em runtime, nunca hardcoded) recebem
chamadas durante o gameplay?** No bloco do primeiro Present (o contexto
real já está em escopo), ler `realCtxVt[12]`/`realCtxVt[13]` e instalar
exatamente **2 hooks novos** via o `IHookEngine` existente (`create` ×2 +
`enableAll()` — único primitivo de enable; `MH_EnableHook(MH_ALL_HOOKS)` é
idempotente para hooks já armados). **Separação estrita probe/real:**
contadores próprios (`realDraw`/`realDrawIndexed` → `rd=`/`rdi=` na linha
`draws` e `rdraws=` no summary — nunca somados em `draws=`), first-fire
próprio (`RLCAP1 first REAL slot=Draw ctx=0x…`), instalação própria
(`RLCAP1 realhook target=Draw addr=0x… ok=1`), veredicto próprio
(`RLCAP1 real verdict2=REALHOOKS_ARMED count=2 total=16` |
`REALHOOKS_FAIL`; summary `rverdict=REALDRAW_ACTIVE | REALDRAW_ZERO |
REALHOOKS_ABSENT`). **Atribuição de módulo** dos 4 endereços draw
(probe/real × Draw/DrawIndexed): `GetModuleHandleExA(FROM_ADDRESS |
UNCHANGED_REFCOUNT)` + `GetModuleFileNameA` (rota barata/segura aprovada;
linha por execução, fora do hot path) → `RLCAP1 owner name=Draw
target=PROBE|REAL addr=… module=…`. **Foco v2 (só medição, nunca gate):**
W = foreground HWND == `OutputWindow` do swapchain real (capturado via
`GetDesc` no discovery) e P = foreground PID == pid do jogo (a métrica
v3.5), reportados separados com os valores crus
(`RLCAP1 focus init W=… P=… sc=0x… fg=0x… fgpid=… pid=…` ;
`focus transition W=a->b P=c->d …`). Re-arm continua a abrir janela nova sem
recarregar DLL/loader (os 2 hooks reais persistem; contadores reiniciam com
a janela). Total: **16 hooks (14 PROBE + 2 REAL)**; loader byte-idêntico;
nenhuma outra área do RenderLift tocada. Leitura dos ramos (a corrida do
utilizador decide): **A** rd/rdi > 0 → o problema era o entry-point do
probe; **B** rd/rdi = 0 → investigar a cadeia CreateDeferredContext/
FinishCommandList/ExecuteCommandList (evidência primeiro — NÃO hooks às
cegas); **C** draws reais > 0 mas om/vp = 0 → usar os draws reais como
entrada de pass/RT; **D** draws periódicos → preservar dados e achar o
padrão. **Resultado: PENDENTE do teste do utilizador (lab pack v3.6).**

## 6. Roadmap da camada

- **0.2 (esta)**: observação, classificação offline, inspector.
- **0.3 `steer`**: classificação corre runtime no backend; redireciona SÓ os
  alvos confirmados para a resolução fixa 640×360; DRS desligado.
- **0.4+ `reconstruct`**: ALRR passa a preencher o display a partir do alvo
  interno; UI decide (uiNative) onde compor.
- **1.0 `full`**: o controlador dinâmico passa a mover a ladder.
