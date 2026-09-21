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

**Leitura do próximo teste:** log até `cp=80` → armado; último `cp=N` +
linha `seh` → passo e endereço exatos da falha; nem `cp=1` → falha na
entrega da thread remota (fora do nosso init), move seguinte: stub de
thread mínimo/injeção alternativa. Binários do lab: branch `dist-pack`
(pack v3, build CI `35652290417`).

## 6. Roadmap da camada

- **0.2 (esta)**: observação, classificação offline, inspector.
- **0.3 `steer`**: classificação corre runtime no backend; redireciona SÓ os
  alvos confirmados para a resolução fixa 640×360; DRS desligado.
- **0.4+ `reconstruct`**: ALRR passa a preencher o display a partir do alvo
  interno; UI decide (uiNative) onde compor.
- **1.0 `full`**: o controlador dinâmico passa a mover a ladder.
