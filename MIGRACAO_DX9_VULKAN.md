# DayZ 0.45.124252 — Migração DirectX 9 → Vulkan

Análise do código e plano de execução.

---

## 0. Resumo executivo

O projeto é a árvore de código do Real Virtuality / DayZ 0.45 (2013), 32-bit, VS2010, com
renderer DirectX 9 de ~86 mil linhas em `lib/d3d9`. Existe um porte parcial para DX10
(`lib/d3dT`, 16.9k linhas) desligado por `_ENABLE_DX10 0` em todas as configurações — ou seja,
a ideia de "segundo backend" já foi tentada e abandonada no meio.

A boa notícia: o D3D9 está **confinado**. O jogo conversa com a interface abstrata `Engine`
(`lib/engine.hpp`, 387 virtuais, 56 puras) e nunca vê tipos D3D. O D3D9 só existe em
`lib/d3d9`, `lib/SimulWeather` e dentro do renderer do Enfusion. O conjunto de chamadas D3D9
realmente usado é estreito: sem fixed-function, sem `D3DXCreateEffect`, sem state blocks,
sem `SetStreamSourceFreq`, sem `ID3DXConstantTable`.

A má notícia: shaders são compilados em runtime com D3DX (`D3DXCompileShader`, 27 sítios),
texturas usam `D3DPOOL_MANAGED` + `LockRect`, e o Enfusion compartilha o mesmo
`IDirect3DDevice9`. Esses três itens concentram ~80% do risco.

**Recomendação: duas entregas separadas.**

| Entrega | O que é | Prazo | Risco |
|---|---|---|---|
| **1. Vulkan já** | Empacotar DXVK (`d3d9.dll`) junto do executável | ~2 semanas | ~zero |
| **2. Vulkan nativo** | RHI própria + backend Vulkan, D3D9 mantido para A/B | 12–18 meses (1 dev) | alto, mas controlado |

A Entrega 1 não é gambiarra: ela valida o alvo, vira o oráculo de comparação por imagem para
todo o resto do projeto, e é o fallback caso a Entrega 2 atrase.

**Não recomendado:** bifurcar `EngineDD9` num `EngineVK` (duplica 27k linhas e congela o
renderer), ressuscitar `lib/d3dT` (DX10 morto, alvo errado), reescrever shaders em SM6 antes
do backend existir, ou atualizar o compilador para VS2022 no início (quebra ABI com
`extern/*.lib` pré-compilados).

---

## 1. O que é o projeto

| Item | Valor |
|---|---|
| Solução | `DayZ.sln`, `DayZ2010.sln` — VS2010 (v100) |
| Configurações | Win32 + Xbox 360. **Sem x64** |
| Executável do jogo | `lib/lib2010.vcxproj` (`WinMain` em `lib/main.cpp`) |
| Criar engine | `lib/main.cpp:12222` → `CreateEngineD3D9(pars)` |
| Implementação | `CreateEngineD3D9` em `lib/d3d9/engdd9.cpp:24058` |
| Build script | `build/buildRelease.bat` → `vcvarsall.bat` (VS11) + MSBuild |

Sem artefatos de build no repositório e **sem controle de versão**. Primeiro passo do plano é
`git init` e um commit do estado atual — sem isso, qualquer refactor de 86k linhas é
irreversível.

---

## 2. Onde o D3D9 realmente está

```
lib/
  engine.hpp          interface abstrata (~387 virtuais)      <- fronteira limpa
  d3d9/               renderer D3D9              ~86k linhas  <- 100% do alvo PC
    engdd9.cpp        27.433 linhas / 971 KB
    engdd9.hpp         5.929 linhas
    postProcess.cpp   10.468 linhas
    postProcess.hpp    2.588 linhas
    txtD3D9.cpp        4.828 linhas
    ShaderSources_3_0/ ~200 KB de HLSL SM3.0
  d3dT/               porte DX10 parcial, DESLIGADO (16.9k linhas)
  d3dXB/              renderer Xbox 360 (irrelevante)
  SimulWeather/       nuvens/céu DX9, LIGADO (4.1k linhas)
Enfusion/src/render/  renderer do middleware, ~10.4k linhas  <- usa o MESMO device
Enfusion/src/materials/ ~6k linhas
```

`EngineDD9` implementa 661 métodos. `EngineDD9` é `friend` de `enf::IRenderer` e
`enf::ShapeCollection`: o Enfusion é desenhado dentro do frame do `EngineDD9` como um passe
de post-process (`PPEnfShapeCollectionRes`, `engdd9.cpp:25753` / `postProcess.cpp:474`).

**Consequência dura:** não existe "portar só o `lib/d3d9`". Enfusion e SimulWeather precisam
ir junto, senão o frame fica incompleto.

---

## 3. Superfície de API realmente usada

Contagem de sítios de chamada em `lib/d3d9`:

| Chamada | Sítios | Implicação Vulkan |
|---|---|---|
| `SetRenderState` | 329 | vira chave de pipeline |
| `SetVertexShaderConstantF` | 203 | vira UBO com offsets dinâmicos |
| `SetPixelShaderConstantF` | 156 | idem |
| `CreateTexture` | 82 | `vkCreateImage` + staging |
| `CreateVertexDeclaration` | 65 | `VkVertexInput*` |
| `CreateVertexBuffer` | 34 | ring buffer (todos `D3DUSAGE_DYNAMIC`) |
| `D3DXCompileShader` | 27 | MojoShader → SPIR-V |
| `CreateIndexBuffer` | 8 | ring buffer |
| `DrawIndexedPrimitive` | 22 | `vkCmdDrawIndexed` |
| `DrawPrimitiveUP` | 11 | staging ring + draw |
| `D3DXCreateEffect` | 0 | — |
| `SetTransform` / `SetTextureStageState` | 0 | fixed-function não é usado |
| `CreateStateBlock` | 0 | — |
| `SetStreamSourceFreq` | 0 | sem instancing D3D9 |
| `ID3DXConstantTable` | 0 | constantes por registro numérico |

Primitivas: apenas `TRIANGLELIST` (8), `TRIANGLESTRIP` (69), `TRIANGLEFAN` (2). **Sem pontos
e sem linhas** — linhas grossas são quads (`DrawLineDo`) e pontos são expandidos no VS
(`VSPoint.hlsl`). Isso elimina os dois piores buracos do Vulkan (`lineWidth > 1`,
`pointSize`).

Alpha test já é feito no shader com `clip()` (`PS.hlsl`, dezenas de ocorrências). Não precisa
de `alphaToCoverage`.

Recursos: `IDirect3DTexture9`/`IDirect3DSurface9` aparecem ~188/182 vezes — a abstração de
recurso é o maior custo mecânico, não a de estado.

Queries: `D3DQUERYTYPE_OCCLUSION` é usado de verdade (`engdd9.cpp:28165`,
`MaxQueries` em `_query[]`) → `VkQueryPool` com `VK_QUERY_CONTROL_PRECISE_BIT`.
`D3DQUERYTYPE_RESOURCEMANAGER` só é usado como sonda de capacidade
(`CreateQuery(..., NULL) == D3D_OK`) → vira constante `true`.

---

## 4. As quatro estratégias

| | Estratégia | Custo | Cobre Enfusion/SimulWeather | Veredito |
|---|---|---|---|---|
| **A** | DXVK drop-in | ~0 | sim | **fazer, como Entrega 1** |
| **B** | Shim D3D9→Vulkan dentro do repo | 20–30k linhas | sim, de graça | reimplementar o DXVK; não |
| **C** | RHI interna + backends D3D9/Vulkan | ~30k linhas novas + refactor mecânico | exige portar os dois | **fazer, como Entrega 2** |
| **D** | `EngineVK` copiando `EngineDD9` | 27k linhas duplicadas | exige portar os dois | nunca |

Por que **C** e não **B**: o `EngineDD9` já tem sombreamento de estado (`RendState`,
`engdd9.hpp:3726`) e gravação de comandos (`D3DCommandBufRec`/`D3DCommandBufPlay`), inclusive
com thread de background. A arquitetura já é "pipeline-friendly" — o `RendState` vira
literalmente a chave do pipeline. Um shim D3D9 teria que reimplementar semântica implícita
(pools gerenciados, `Lock`, renomeação de recursos, predicação), que é exatamente onde o DXVK
gastou anos. Não faz sentido reimplementar o que já existe e funciona.

Por que **C** e não **D**: a interface `Engine` já é a fronteira certa, mas copiar a
implementação duplica bugs e proíbe correções nos dois lados. Com RHI, D3D9 e Vulkan
compartilham o mesmo código de renderer e podem ser comparados frame a frame.

---

## 5. Arquitetura alvo

```
        jogo / simulação
               |
          Engine (lib/engine.hpp)          <- inalterado
               |
          EngineDD9 (lib/d3d9)             <- inalterado na lógica
               |
     +---------+----------+
     |                    |
  RendState          IRHIDevice            <- NOVO: lib/rhi/
  (pipeline key)          |
                +---------+---------+
                |                   |
            RHID3D9             RHIVulkan
          (atual, ref.)        (novo, alvo)
                |                   |
        IDirect3DDevice9      VkDevice/VkQueue
```

`IRHIDevice` deve ser **modelada em D3D9, não em Vulkan**: estado global + draw imediato.
Isso mantém o diff do `lib/d3d9` mecânico (troca de tipo e de nome de método) em vez de
conceitual. O backend Vulkan é quem faz o trabalho de traduzir estado global em pipelines.

Interface mínima (nomes, não código):

```
IRHIDevice
  BeginFrame / EndFrame / Present
  SetRenderState(state, value)              // mesma enumeração D3D9
  SetSamplerState(stage, state, value)
  SetVertexShaderConstantF(reg, data, n)
  SetPixelShaderConstantF(reg, data, n)
  SetTexture(stage, IRHITexture*)
  SetVertexDeclaration / SetVertexShader / SetPixelShader
  SetStreamSource / SetIndices
  DrawIndexedPrimitive / DrawPrimitive
  Clear
  SetRenderTarget / SetDepthStencil
  StretchRect / GetRenderTargetData
  CreateQuery / QueryIssue / QueryGetData
  CreateTexture / CreateVertexBuffer / CreateIndexBuffer
  CreateShader(bytecode) / CreateVertexDeclaration(elements)
  OnLostDevice / OnResetDevice / EvictManagedResources
```

`IRHITexture`, `IRHIBuffer`, `IRHIShader`, `IRHIVertexDecl`, `IRHIQuery` são opacos.

---

## 6. Fases

### Fase 0 — Baseline reproduzível (2–4 semanas)

1. `git init` + commit de tudo. **Bloqueante.**
2. Reproduzir o build: VS2010 SP1 + DirectX SDK (June 2010) + `build/buildRelease.bat`.
   Verificar `bin/` e rodar o jogo.
3. Empacotar DXVK 32-bit (`d3d9.dll` + `dxgi.dll`) ao lado do `.exe`. Validar que o jogo
   renderiza em Vulkan.
4. Criar o **harness de imagens de referência**:
   - cena fixa, câmera fixa, hora do dia fixa, seed fixa;
   - comando de console que renderiza N frames e salva PNG;
   - comparador SSIM/PSNR (`libpng` já está no repo);
   - rodar em D3D9 e em DXVK, guardar os dois conjuntos.
5. Instrumentar: logar por frame draw calls, mudanças de estado, binds de textura, bytes
   enviados, picos de memória. Exportar CSV. **É esse log que dimensiona as fases 2–4** —
   não estime pelo número de linhas.

Entregável: build reproduzível, jogo rodando em Vulkan via DXVK, e um teste de regressão
visual automatizado.

### Fase 1 — RHI + backend D3D9 (8–12 semanas)

1. Criar `lib/rhi/` com a interface acima.
2. Portar `D3DDeviceServant` (que já é o wrapper de device, `engdd9.hpp:1370`) para
   `RHID3D9`. É aqui que estão ~90% das chamadas D3D9 — um único arquivo concentra o
   trabalho.
3. Portar `RendState` para escrever via `IRHIDevice`.
4. Portar `txtD3D9.cpp` (texturas) e `d3d9Buffers.cpp` (buffers) para as interfaces de
   recurso.
5. `EngineDD9` passa a receber `IRHIDevice*` em vez de `D3DDeviceServant*`.
6. **Critério de aceite: zero diferença visual** no harness. Se houver diferença, é bug de
   refactor, não de backend.

Esta fase não muda comportamento nenhum. É refactor puro, verificado por imagem. É a fase
que compra o direito de fazer a Fase 2.

### Fase 2 — Backend Vulkan: dispositivo, swapchain, memória (12–16 semanas)

1. Instance/device: Vulkan 1.1 como baseline (1.3 se disponível, com fallback).
   `VK_KHR_dynamic_rendering` ou `VK_KHR_dynamic_rendering_local_read` quando existir —
   evita `VkRenderPass`/`VkFramebuffer` e é muito mais próximo da semântica D3D9.
2. Swapchain: `D3DPRESENT_PARAMETERS` → `VkSwapchainKHR`. `D3DSWAPEFFECT_DISCARD` mapeia
   bem para o modelo de acquire/present. `Present` com vsync → `VK_PRESENT_MODE_FIFO`.
   Fullscreen exclusivo → borderless + `VK_EXT_full_screen_exclusive` quando disponível.
3. Alocador de memória próprio (sub-alocação por heap/tipo). Não usar `vkAllocateMemory`
   por recurso.
4. Buffers: `D3DUSAGE_DYNAMIC` + `Lock(DISCARD)` → ring buffer por frame-in-flight (3
   frames). `DrawPrimitiveUP` → staging ring.
5. Texturas: upload via staging, `vkCmdCopyBufferToImage`, respeitando
   `bufferImageGranularity`. `D3DPOOL_MANAGED` → **sem cópia CPU por padrão**; cópia CPU só
   para as texturas que sofrem `LockRect` depois da criação (levantar essa lista na Fase 0
   com instrumentação — provavelmente fontes, vídeo, RTs dinâmicos e camadas de terreno).
6. `EvictManagedResources` → LRU explícito com orçamento de memória.
7. Queries: `VkQueryPool` para oclusão; `RESOURCEMANAGER` vira constante.
8. `StretchRect` → `vkCmdBlitImage` (com filtro) ou `vkCmdCopyImage`. Atenção: blit de
   profundidade tem suporte limitado por formato — o downsample de depth em
   `engdd9.cpp:14185` pode precisar de passe com shader.
9. Device lost: D3D9 tinha `Reset` e retry de 60 s (`engdd9.cpp:22355`). Vulkan não tem
   recuperação — `VK_ERROR_DEVICE_LOST` é fatal. **Isso apaga código**, não adiciona.
   `Reset` vira apenas recriação de swapchain.

### Fase 3 — Pipelines e shaders (8–12 semanas)

Ver seção 7 e 8. É a fase de maior risco técnico.

### Fase 4 — Render targets, profundidade e post-process (8–12 semanas)

`postProcess.cpp` tem 10.468 linhas, mas quase tudo é passe full-screen com PS. O trabalho
real está em:

- `D3DRS_SRGBWRITEENABLE` é estado **por draw**; em Vulkan sRGB é propriedade do **formato da
  image view**. Solução: para cada RT que precisa de sRGB, criar duas views (UNORM e SRGB) e
  escolher no framebuffer. Isso afeta a Fase 2 e precisa ser decidido cedo.
- Leitura de RT enquanto ele está bound como target: D3D9 permite (com resultado indefinido
  mas funcionando); Vulkan exige barrier explícita e `VK_IMAGE_LAYOUT_GENERAL` ou
  desbind. **Auditar** os casos com o log da Fase 0.
- `D3DFMT_INTZ`, `DF16`, `DF24` (hacks de depth-texture de NVIDIA/ATI) desaparecem:
  usar `VK_FORMAT_D32_SFLOAT` (ou `D32_SFLOAT_S8_UINT`) e amostrar direto.
- MSAA: `D3DMULTISAMPLE_*_SAMPLES` → `VkSampleCountFlagBits`. "Quality levels" do D3D9 não
  existem; ignorar.
- `D3DXSaveSurfaceToFile` (screenshot, `engdd9.cpp:3779`) → `vkCmdCopyImageToBuffer` +
  `libpng`.

### Fase 5 — Enfusion (4–8 semanas)

`Enfusion/include/render/enf_rendmain.h` já tem a abstração `ENF_DX9`/`ENF_OGL`. Adicionar
`ENF_VULKAN` seguindo o mesmo padrão é o caminho de menor atrito — o padrão já existe no
código, só falta a terceira implementação.

`enf_rendinitDZ.cpp` pega o device via `GEngineDD->GetDirect3DDevice()`. Trocar por
`IRHIDevice*`.

### Fase 6 — SimulWeather (2–4 semanas)

`lib/SimulWeather` (4.1k linhas) é DX9 puro (`IDirect3DTexture9`). `extern/SimulWeather` tem
backends DX10/DX11/OpenGL — usar o DX11 como referência de port.

**Atalho aceitável:** `_ARMA3_SIMUL_WEATHER 0` na primeira milestone Vulkan e portar depois.
Nuvens somem, o resto funciona. É a única feature que vale cortar temporariamente.

### Fase 7 — Estabilização e performance (8–16 semanas)

Validation layers em build debug, RenderDoc, comparação contínua de imagens, perfil de GPU,
orçamento de memória em 32-bit.

---

## 7. Shaders

Situação atual:

- HLSL SM3.0 (`vs_3_0` / `ps_3_0`, fallback SM2), ~200 KB em `ShaderSources_3_0/`.
- `PS.hlsl` 87 KB, `VS.hlsl` 44 KB, `common.h` 27 KB, `psvs.h` 20 KB.
- ~110 entry points de PS, 7 de VS, 7 variantes `PixelShaderType` → ~900 permutações
  estimadas de PS.
- Constantes em registros numéricos (`register(cN)`, `register(sN)`), tipadas por macros
  `VSC_LIST`/`PSC_LIST`.
  `FREESPACE_START_REGISTER 46`, `LIGHTSPACE_START_REGISTER_VS 214`,
  `LIGHTSPACE_START_REGISTER_PS 200`, `MAX_LIGHTS 5`, `TEXID_SHADOWMAP 15`.
- Compilação em runtime via `D3DXCompileShader_Cached` (27 sítios) + cache em disco `.shdc`
  (`ShaderCompileCache.cpp`).

**Estratégia recomendada: manter o HLSL e traduzir o bytecode.**

```
HLSL SM3 (inalterado)
   └─ fxc /T vs_3_0|ps_3_0     (build time, não runtime)
        └─ bytecode DXBC
             └─ MojoShader (MOJOSHADER_PROFILE_SPIRV)
                  └─ SPIR-V 1.0 → vkCreateShaderModule
```

Por quê:

1. **Zero mudança nos shaders.** 200 KB de HLSL continuam sendo a fonte da verdade.
2. **O layout de constantes sobrevive.** MojoShader traduz os registros `cN` para um UBO com
   o mesmo layout. Ou seja, `SetVertexShaderConstantF(reg, data, count)` continua sendo um
   `memcpy` no offset certo do UBO do frame — o caminho de upload de constantes não muda.
3. **Determinístico e sem D3DX em runtime.** Compilar as ~900 permutações no build gera
   ~15 MB de SPIR-V. Melhor que compilar em runtime na máquina do jogador.
4. **Comparável.** Dá para renderizar o mesmo frame em D3D9 e Vulkan e comparar.

Riscos e mitigação:

| Risco | Mitigação |
|---|---|
| MojoShader é C99; VS2010 tem `snprintf` só a partir de VS2015 | shim `#define snprintf _snprintf` ou compilar como C++ com cast de `void*`; **testar na semana 1** |
| MojoShader cobre SM1–3, mas com quirks | validar as ~900 permutações contra o harness de imagem |
| `tex2Dlod`, `tex2Dgrad`, `ddx/ddy`, `clip`, `texkill` | todos suportados; verificar caso a caso no log de compilação |
| Precisão (`half` vs `float`) | Vulkan não tem `half` implícito; MojoShader emite `float` — diferença visual mínima, mas medir |

Alternativa (só se MojoShader falhar): reescrever para SM5/6 e usar DXC. Custo alto, e
inverte a ordem de risco — não começar por aí.

`lib/ShadersCommon/*.psi` são assembly DX8 e **não são referenciados por nada**. Ignorar.

---

## 8. Estados → pipelines

O `RendState` (`engdd9.hpp:3726`) já sombreia todo o estado D3D9. Ele vira a chave do cache
de pipelines:

```
PipelineKey {
  vs, ps, vertexDecl,
  blend (enable, src/dst, op, separate alpha, write mask),
  depth (test, write, func, bias, slopeBias),
  stencil (enable, funcs, ops, masks, two-sided),
  raster (cull, frontFace, fillMode, depthClip),
  renderTargetFormats[], depthFormat, sampleCount,
  sRGBWrite
}
```

| Estado D3D9 | Vulkan |
|---|---|
| `D3DRS_ZENABLE/ZWRITEENABLE/ZFUNC` | `VkPipelineDepthStencilStateCreateInfo` |
| `D3DRS_ALPHABLENDENABLE` + fatores | `VkPipelineColorBlendAttachmentState` |
| `D3DRS_SEPARATEALPHABLENDENABLE` | dual-source blending (`VK_BLEND_FACTOR_SRC1_*`), requer `dualSrcBlend` |
| `D3DRS_STENCILENABLE` + two-sided | stencil front/back — nativo |
| `D3DRS_CULLMODE` | `VkPipelineRasterizationStateCreateInfo.cullMode` |
| `D3DRS_FILLMODE` (wireframe) | `VK_POLYGON_MODE_LINE` + `fillModeNonSolid` |
| `D3DRS_DEPTHBIAS` / `SLOPESCALEDEPTHBIAS` | dinâmico (`vkCmdSetDepthBias`) |
| `D3DRS_SRGBWRITEENABLE` | formato da view (ver Fase 4) |
| `D3DRS_COLORWRITEENABLE` | `colorWriteMask` |
| `D3DRS_ALPHATESTENABLE` | já é `clip()` no shader — ignorar |
| `D3DRS_FOG*` | fog por vértice no VS — ignorar |
| `D3DRS_POINTSIZE` | pontos já são quads — ignorar |

Estado dinâmico em Vulkan: viewport, scissor, depth bias, stencil ref/mask. Todo o resto é
estático na pipeline.

O `_backgroundD3D` / `D3DCommandBufRec` já grava comandos numa thread e reproduz na
principal. Isso mapeia em **secondary command buffers** (ou primárias gravadas por thread),
com `VkCommandPool` por thread.

Constantes: um UBO por frame-in-flight com o array de registros (VS 256, PS 224, mais
light-space). `SetVertexShaderConstantF` escreve no UBO do frame corrente; o draw usa
`vkCmdBindDescriptorSets` com `dynamicOffset`. Sem alocação de descriptor por draw.

---

## 9. Formatos

| D3D9 | Vulkan |
|---|---|
| `A8R8G8B8` | `B8G8R8A8_UNORM` |
| `X8R8G8B8` | `B8G8R8A8_UNORM` (alfa ignorado) |
| `A2R10G10B10` | `A2R10G10B10_UNORM_PACK32` (fallback + swizzle se ausente) |
| `R5G6B5` | `B5G6R5_UNORM_PACK16` |
| `A4R4G4B4` | `B4G4R4A4_UNORM_PACK16` |
| `A8` / `L8` | `R8_UNORM` (L8 → sample `.rrr`) |
| `A8L8` | `R8G8_UNORM` |
| `V8U8` | `R8G8_SNORM` |
| `DXT1/2/3/4/5` | `BC1/BC2/BC3_RGBA_UNORM_BLOCK` |
| `R32F` | `R32_SFLOAT` |
| `G16R16F` | `R16G16_SFLOAT` |
| `A16B16G16R16F` | `R16G16B16A16_SFLOAT` |
| `A32B32G32R32F` | `R32G32B32A32_SFLOAT` |
| `D16` | `D16_UNORM` |
| `D24S8` | `D24_UNORM_S8_UINT` |
| `D24X8` | `D32_SFLOAT` |
| `INTZ` / `DF16` / `DF24` | `D32_SFLOAT` (+ `_S8_UINT` se stencil) |

Formatos PAA/PAC são containers de arquivo, não de GPU — `lib/pactext.hpp` continua
decodificando em CPU e o resultado é uploaded normalmente.

`D3DXLoadSurfaceFromFileInMemory` pode continuar sendo usado para **decodificação em CPU**
(não toca no device). É o caminho preguiçoso e correto.

---

## 10. Build e toolchain

- **Manter VS2010** durante todo o porte. Os `extern/*.lib` (Bullet, Botan, TBB, curl,
  Steamworks, OpenAL, Ogg/Vorbis, Theora, zlib, libpng) foram compilados com v100; trocar de
  compilador quebra ABI e vira um projeto paralelo.
- VS2010 compila a API C do Vulkan sem problema (`<stdint.h>` existe, `vulkan-1.lib` x86
  existe no SDK). Usar a API C, não `vulkan.hpp`.
- `vulkan-1.dll` do sistema; loader via `vulkan-1.lib`.
- MojoShader compilado como C no mesmo projeto — testar cedo (ver riscos em §7).
- `fxc` do DXSDK June 2010 para compilar SM3 no build.
- 32 bits é suportado pelo Vulkan, mas o espaço de endereço é um risco real (§11).

Atualizar para VS2022 só **depois** que o backend Vulkan estiver estável, e como projeto
separado (recompilar todos os `extern`).

---

## 11. Riscos

| # | Risco | Impacto | Mitigação |
|---|---|---|---|
| 1 | Fidelidade do MojoShader nas ~900 permutações | alto | harness de imagem; D3D9 vivo para A/B |
| 2 | `D3DPOOL_MANAGED` + `LockRect` sem equivalente | alto | instrumentar na Fase 0; cópia CPU só onde há lock |
| 3 | 32-bit: driver Vulkan + jogo + texturas | alto | orçamento de memória; reduzir teto de textura se preciso |
| 4 | Enfusion acoplado ao device | médio | `ENF_VULKAN` seguindo padrão existente |
| 5 | `sRGBWrite` por draw vs por formato | médio | decidir na Fase 2, duas views por RT |
| 6 | Ler RT enquanto bound | médio | auditar com log; barrier explícita |
| 7 | Sem testes automatizados hoje | alto | Fase 0 constrói o harness antes de tudo |
| 8 | Sem controle de versão | crítico | `git init` no dia 1 |
| 9 | Drivers antigos / GPUs sem Vulkan 1.1 | médio | baseline 1.1; DXVK cobre o resto |
| 10 | SimulWeather DX9 | baixo | desligar na primeira milestone |

---

## 12. Estimativas

Premissas: 1 dev sênior em tempo integral, código de 2013, sem testes existentes.

| Fase | Esforço |
|---|---|
| 0 — baseline + DXVK + harness | 2–4 semanas |
| 1 — RHI + backend D3D9 | 8–12 semanas |
| 2 — Vulkan: device/swapchain/memória | 12–16 semanas |
| 3 — pipelines + shaders | 8–12 semanas |
| 4 — RTs, depth, post-process | 8–12 semanas |
| 5 — Enfusion | 4–8 semanas |
| 6 — SimulWeather | 2–4 semanas |
| 7 — estabilização/perf | 8–16 semanas |
| **Total** | **~12–18 meses** |

Com 2 devs, fases 2–4 paralelizam por subsistema: ~8–12 meses.

A Entrega 1 (DXVK) entrega Vulkan funcional em ~2 semanas e não depende de nada disso.

---

## 13. O que não fazer

- **Não bifurcar `EngineDD9`.** 27k linhas duplicadas, correções em dobro, divergência
  permanente.
- **Não ressuscitar `lib/d3dT`.** 16.9k linhas de DX10 desligado; DX10 não é o alvo e o
  código está incompleto.
- **Não reescrever shaders primeiro.** O backend precisa existir para validar os shaders.
- **Não trocar o compilador primeiro.** ABI dos `extern` quebra.
- **Não portar para 64-bit ao mesmo tempo.** É um projeto separado e maior que o Vulkan.
- **Não perseguir recursos Vulkan 1.3** (bindless, descriptor indexing, mesh shaders) antes
  de ter paridade visual. Isso é otimização, não porte.
- **Não apagar `lib/d3dT`, `lib/d3dXB` nem `lib/ShadersCommon`.** São código morto, mas são
  referência e podem ser trabalho do usuário.

---

## 14. Primeiros dez passos

1. `git init` + commit do estado atual.
2. Instalar VS2010 SP1 + DirectX SDK June 2010.
3. Compilar e rodar o jogo em D3D9. Confirmar que funciona.
4. Copiar DXVK 32-bit ao lado do `.exe`. Confirmar que roda em Vulkan.
5. Criar cena de teste determinística + comando de screenshot.
6. Escrever o comparador de imagens (SSIM).
7. Instrumentar draw calls / estados / binds / memória por frame; gerar CSV.
8. Medir a carga real de um frame representativo.
9. Desenhar `IRHIDevice` com base nos números do passo 8, não nos 27k linhas.
10. Começar a Fase 1 pelo `D3DDeviceServant`, com o harness verde a cada commit.

O passo 8 é o que decide o cronograma real. Tudo acima é estimativa até ele existir.
