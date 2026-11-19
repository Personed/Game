# DayZ 0.45.124252 — Modernização para 64 bits + Visual Studio 2026

Análise e plano. Substitui a **ordem** do plano anterior, não o objetivo: o
`MIGRACAO_DX9_VULKAN.md` continua válido depois disto.

---

## 0. Resumo executivo

Duas coisas precisam ser ditas antes do plano.

**1. Isto não é uma alternativa ao Vulkan.** D3D9 funciona em x64 — o DXSDK June 2010 que
já está no repo tem `d3d9.lib` e `d3dx9.lib` x64. Modernizar para x64/VS2026 deixa o jogo
com mais memória, melhor debug e compilador atual, mas ele **continua em D3D9**. Se o
objetivo final é Vulkan, esta é a fase 0 ideal: você chega no porte gráfico com toolchain
moderna e espaço de endereço de sobra.

**2. Não faça as duas mudanças de uma vez.** VS2026 e x64 juntos tornam cada bug ambíguo
("quebrou por causa do compilador ou do tamanho do ponteiro?"). Duas etapas verificáveis:

| Etapa | O que muda | Esforço (1 dev) | Critério de aceite |
|---|---|---|---|
| **A** | VS2010/v100 → VS2026/v145, **ainda Win32** | 4–8 semanas | `Retail\|Win32` compila e o jogo roda |
| **B** | Win32 → **x64** | 8–16 semanas | `Retail\|x64` compila, roda e salva/carrega |

Total realista: **3–6 meses**. Muito menos que o porte Vulkan, e o progresso é binário:
o jogo abre ou não abre.

---

## 1. Estado atual medido

| Item | Valor |
|---|---|
| Solução | `DayZ2010.sln` — VS2010, `Format Version 11.00` |
| Projetos na solução | **6**: `cfg`, `lib`, `LaunchServer`, `BulletCollision`, `BulletDynamics`, `LinearMath` |
| Configurações da solução | **13**, todas `Win32` |
| Código em `lib` | 716 arquivos, 28,3 MB |
| `lib2010.vcxproj` | 635 `ClCompile`, 809 `ClInclude`, 6.679 linhas |
| Enfusion compilado dentro de `lib` | 80 arquivos |
| Toolset | `v100` (`_MSC_VER 1600`) |
| Runtime | `/MT` (CRT estática) em todas as configs |
| Plataformas no `.vcxproj` | `Win32` + `Xbox 360` |
| Consciência de 64 bits | **zero** — nenhum `_WIN64`, `_M_X64` ou `PLATFORM_64` em `lib` |

O número de projetos é a melhor notícia do diagnóstico: migrar 6 `.vcxproj` é trabalho de
dias, não de meses.

---

## 2. O que o Visual Studio 2026 muda

| | VS2010 | VS2026 |
|---|---|---|
| Toolset | v100 | **v145** |
| MSVC | 14.0 (1600) | **14.50/14.51 (1950/1951)** |
| Padrão C++ | C++03 + migalhas de C++11 | C++14/17/20 completos, C11/C17 |
| SDK do Windows | SDK 7.0/7.1 | SDK 10/11 |

Fatos que definem o plano:

- **Compatibilidade binária só a partir do VS2015 (v140).** `v100` não é compatível: todo
  `.lib` C++ precisa ser recompilado.
- **VS2026 instala toolsets até o VS2015; não instala o v100.** Não existe "usar VS2026 com
  o compilador antigo".
- **VS2026 não consegue targetar Windows 7/8/8.1.** O mínimo passa a ser **Windows 10**.
  Para um jogo de 2013 isso é uma decisão de produto, não técnica.
- VS2026 e VS2010 convivem lado a lado — mantenha o VS2010 instalado até a Etapa A ficar
  verde, para comparar comportamento.

---

## 3. Boa notícia: o código é limpo

Levantamento das quebras típicas de C++ moderno:

| Item | Ocorrências reais | Ação |
|---|---|---|
| `std::auto_ptr` | **1** (`registration.cpp:72`) | `std::unique_ptr` |
| `std::random_shuffle` | **2** (`spawn.cpp:1500,1649`) | `std::shuffle` + `std::mt19937` |
| `throw()` | 0 | — |
| `bind1st` / `bind2nd` / `unary_function` | 0 (só em `autoexp.dat`) | — |
| `register` | 0 | — |
| `__asm` real | **1** (`fixed.hpp:24`) | já tem fallback no arquivo |
| `__asm nop` | ~40 | `__nop()` |
| Ponteiro → inteiro explícito | **2** (`roads.cpp:3375,3379`) | `intptr_t` |
| `SetWindowLong` com ponteiro | 0 (só `GWL_STYLE`, que é 32-bit) | — |

O `fixed.hpp` já contém a implementação correta para 64 bits, só desativada por um
`#ifdef _MSC_VER` que continua verdadeiro no x64:

```cpp
// lib/fixed.hpp — trocar a condição, não o corpo
#if defined(_MSC_VER) && !defined(_M_X64)
    __asm { ... }                       // x86
#else
    return (int)(((long long)a * d) >> 16);   // já existe, matematicamente equivalente
#endif
```

Ou seja: a Etapa A é quase toda projeto, dependência e warning. A Etapa B é quase toda
auditoria de tipo.

---

## 4. Má notícia: onde realmente dói

### 4.1 Anti-tamper em assembly, ativo no Retail

`retailConfig.h` define `_SUPER_RELEASE=1`. Em `integrity.hpp:14`:

```cpp
#if _SUPER_RELEASE && defined _WIN32
# define PROTECTION_ENABLED 1
```

Com isso, compilam em assembly x86 no build de produção:

- `integrity.hpp` — `IF_FADE()` usa `lea eax, DoFade` (linha 210).
- `input.cpp:1185` — trampoline `jmp Skip` + `_emit` de magic numbers.

Nenhum dos dois tem equivalente em x64. **Decisão necessária:** manter a proteção
(reescrever em C++ com o mesmo efeito, perdendo a ofuscação) ou desligar no x64
(`#if PROTECTION_ENABLED && !defined(_M_X64)`). Para um projeto de modernização, desligar é
o caminho honesto; a proteção já não protege nada que valha a pena em 2026.

O bloco grande de `CheckGenuine()` está dentro de `#if 0` — esse já é morto.

### 4.2 Bibliotecas x86 sem fonte

Ver §7. É o trabalho mais chato e o que mais pode atrasar.

### 4.3 `/MT` em tudo

CRT estática significa que **toda** biblioteca recompilada precisa de `/MT` (ou `/MTd`) e o
mesmo `_ITERATOR_DEBUG_LEVEL`. Misturar Debug/Release ou `/MT` com `/MD` gera erro de linker
obscuro. Regra: cada `.lib` é construída na mesma configuração do `.exe`.

### 4.4 x87 → SSE muda resultados

No x86 o MSVC usa x87 com intermediários de 80 bits; no x64 usa SSE com 32/64 bits. Física,
interpolação e render vão divergir na última casa decimal. Não é bug, mas:

- testes de imagem vão acusar diferença;
- replays/saves antigos podem divergir;
- **não** compense com `/fp:fast` — isso piora.

### 4.5 Treze configurações

`Buldozer`, `Debug Server`, `Debug`, `Profile`, `Release Demo`, `Release LTCG`, `Release`,
`ReleaseNoOpt`, `Retail Demo`, `Retail Server`, `Retail Steam`, `Retail`, `Testing`.

Adicionar x64 dobra isso para 26 combinações. **Pode antes de duplicar.**

---

## 5. Etapa A — VS2026, ainda Win32 (4–8 semanas)

### 5.1 Preparação

1. `git init` + commit de tudo. **Bloqueante** — não existe controle de versão hoje.
2. Instalar VS2026 (workload "Desenvolvimento para desktop com C++") + Windows 10/11 SDK.
3. Manter VS2010 instalado.
4. Confirmar que o build VS2010 atual funciona. É a linha de base.
5. Criar branch `vs2026-win32`.

### 5.2 Podar configurações

Antes de tocar no toolset, reduzir a matriz. Sugestão: manter `Debug`, `Release`, `Retail`,
`Retail Steam`, `Retail Server`. Arquivar as demais numa branch. Isso corta a Etapa B pela
metade.

### 5.3 Projetos

Editar o XML diretamente, não pelo wizard — assim o diff é revisável:

| Propriedade | De | Para |
|---|---|---|
| `PlatformToolset` | `v100` | `v145` |
| `WindowsTargetPlatformVersion` | ausente | `10.0` |
| `ToolsVersion` | `4.0` | atual |
| Configs `Xbox 360` | presentes | **remover** (VS2026 não tem toolset) |
| `.sln` | `Format Version 11.00` | converter ao abrir / regenerar |

Fazer nos 6 projetos: `cfg`, `lib`, `LaunchServer`, `BulletCollision`, `BulletDynamics`,
`LinearMath`.

### 5.4 Código

1. `/std:c++17`.
2. `auto_ptr` → `unique_ptr` (1 sítio).
3. `random_shuffle` → `shuffle` (2 sítios).
4. `__asm nop` → `__nop()` (mecânico).
5. Corrigir `#pragma warning` inválidos (C4619 "unknown warning number").
6. **Não** ligar `/permissive-` ainda. Deixar para depois que compilar.
7. **Não** ligar `/W4` ainda. `/W3` basta nesta etapa.

### 5.5 Dependências (todas Win32, v145, `/MT`)

| Dependência | Fonte no repo | Ação na Etapa A |
|---|---|---|
| Bullet 2.76 | sim (145 cpp, 3 `.vcxproj`) | trocar toolset para v145 |
| SimulWeather | sim (158 cpp, projetos VC10/VC12) | trocar toolset para v145 |
| Speex 1.0.5 / 1.2 | sim | compilar `/MT` v145 |
| Vorbis / Ogg / Theora | sim | compilar `/MT` v145 |
| zlib | **não** | compilar do upstream, `/MT` |
| libpng | **não** | compilar do upstream, `/MT` |
| OpenAL | só headers | manter o `.lib` x86 existente por enquanto |
| Steamworks / HASP / RSA | libs x86 | manter por enquanto |

Nesta etapa **nada precisa virar x64** — só precisa linkar com v145. Bibliotecas C puras
(zlib, libpng, speex, vorbis, ogg, theora) costumam linkar entre versões; as C++ (Bullet,
SimulWeather) não, e precisam ser recompiladas.

`dxerr9.lib` (nome do DXSDK antigo) não existe no June 2010 — o correto é `DxErr9.lib`.
Ajustar o `#pragma comment` em `engdd9.cpp:1825`.

### 5.6 Critério de aceite

- `Retail|Win32` compila sem erro.
- O jogo abre, entra numa cena e renderiza.
- Comparação visual contra o build VS2010: idêntica (ou com diferenças explicadas por FP).

---

## 6. Etapa B — x64 (8–16 semanas)

### 6.1 Projetos

1. Adicionar plataforma `x64` aos 6 projetos e às configurações da solução. Fazer por
   script — são dezenas de blocos repetidos.
2. `AdditionalLibraryDirectories`: trocar os caminhos fixos
   `extern\dxsdk\lib\x86` por `extern\dxsdk\lib\$(PlatformTarget)`.
3. `extern\SimulWeather\Simul\lib\Win32\VC10\...` → caminho x64 equivalente (gerado na §6.5).
4. Remover `extern\RotorLib\Lib\vc8-32\release\` das library dirs.
5. `TargetMachine` / `MinimumRequiredVersion` conforme o padrão do v145.

### 6.2 Assembly

| Arquivo | Situação | Ação |
|---|---|---|
| `lib/fixed.hpp:24` | vivo | guardar com `!defined(_M_X64)` — fallback já existe |
| `lib/integrity.hpp:210` | vivo no Retail | `#if PROTECTION_ENABLED && !defined(_M_X64)` ou reescrever |
| `lib/input.cpp:1185` | vivo no Retail | idem |
| `__asm nop` (~40) | vivo | `__nop()` |
| `lib/d3dXB/engD3DXB.cpp` | Xbox, não compila | ignorar |
| `El/Math/matrix.asm` | fora de qualquer projeto | ignorar |
| `extern/SimulWeather/.../Callx64.asm` | já é x64 | ok |

### 6.3 Truncamento de ponteiro

Estratégia: deixar o compilador achar.

1. Compilar x64 com `/W4`.
2. Coletar **C4311** (truncamento ponteiro→tipo menor), **C4312** (tipo menor→ponteiro),
   **C4244**, **C4267** (`size_t`→`int`).
3. Temporariamente `/we4311 /we4312` para forçar correção.
4. Corrigir caso a caso: `intptr_t`/`uintptr_t`/`size_t`/`DWORD_PTR`.
5. `roads.cpp:3375,3379` — `reinterpret_cast<long>` → `intptr_t` (ou, melhor, índice).

Casos que o compilador **não** pega e precisam de grep manual:

- `DWORD`/`UINT` guardando `HANDLE` ou ponteiro;
- `int` recebendo `strlen`/`sizeof`;
- `%d`/`%x` em `printf` com ponteiro;
- `SendMessage`/`PostMessage` passando ponteiro por `WPARAM`/`LPARAM` sem cast.

### 6.4 Serialização e layout de struct

O risco silencioso. Um struct com `void*` ou `long` muda de tamanho e o arquivo/rede quebra
sem aviso.

1. Grep por `#pragma pack`, `fwrite`/`fread` de struct, `Write(`/`Read(` de struct cru.
2. Verificar se saves usam `ParamFile` (texto) ou binário cru. Se for `ParamFile`, está
   seguro.
3. Protocolo de rede: cliente e servidor novos são consistentes entre si, mas **cliente
   antigo não conecta em servidor novo**. Se isso importar, versionar o protocolo.
4. Bitfields que assumem 32 bits.

### 6.5 Dependências x64

| Dependência | x64 no repo | Ação |
|---|---|---|
| DXSDK June 2010 | **sim** (`d3d9`, `d3dx9`, `DxErr9`, `dxguid`, `dinput8`, `dsound`, `ddraw`, `X3DAudio`, `xapobase`, `XAPOFX`) | nenhuma |
| Intel VTune notify | **sim** (`extern/Intel/lib64`) | nenhuma |
| Bullet 2.76 | não | adicionar config x64 |
| SimulWeather | não | recompilar x64 (fonte + projetos presentes) |
| Speex / Vorbis / Ogg / Theora | não | recompilar x64 |
| zlib / libpng | não | compilar x64 do upstream |
| OpenAL | não | OpenAL Soft x64 |
| Steamworks | não | SDK x64 da Valve |
| HASP | não | build x64 do fornecedor |
| RSA | não | recompilar se houver fonte; senão, fornecedor |
| RotorLib | não | **irrelevante**: `_HELISIM_ROTORLIB 0` em `normalConfig.h` e `retailConfig.h` |
| EAX / EFX / xlive / msxml2 / mpiwin32 / IFC22 / LVCGame / siapp / tngaming / VoiceCmds / ClientLib / ode / liblzma | não | auditar uso real; provavelmente remover |

### 6.6 Critério de aceite

- `Retail|x64` compila e linka.
- O jogo abre, renderiza e joga.
- Save/load funciona.
- Servidor e cliente x64 conversam.
- Sem crash por stack overflow (x64 usa frames maiores; se preciso, aumentar `/STACK`).

---

## 7. Flags recomendadas

| Flag | Etapa A | Etapa B | Motivo |
|---|---|---|---|
| `/std:c++17` | sim | sim | 3 quebras no total |
| `/Zc:__cplusplus` | sim | sim | bibliotecas modernas dependem |
| `/Zc:inline` | sim | sim | padrão moderno |
| `/MT` | manter | manter | CRT estática; não misturar |
| `/fp:precise` | manter | manter | **nunca** `/fp:fast` |
| `/W3` | sim | — | Etapa A |
| `/W4` | não | sim | achar truncamento |
| `/permissive-` | não | depois | quebra código legado |
| `/utf-8` | testar | — | só se os fontes forem UTF-8; se forem Latin-1, usar `/source-charset:.1252` |
| `/GS`, `/guard:cf`, `/sdl` | não | depois | endurecimento, não porte |
| `/arch:AVX2` | não | não | muda FP; otimização posterior |

---

## 8. Riscos

| # | Risco | Impacto | Mitigação |
|---|---|---|---|
| 1 | Anti-tamper asm no Retail | alto | decidir: desligar no x64 ou reescrever |
| 2 | Libs x86 sem fonte (OpenAL, HASP, RSA, Steam) | alto | levantar antes da Etapa B; algumas podem ser removidas |
| 3 | Serialização dependente de tamanho de ponteiro | alto | auditoria na §6.4 |
| 4 | x87 → SSE muda física/render | médio | esperado; comparar, não "corrigir" |
| 5 | `/MT` + Debug/Release misturados | médio | regra única por config |
| 6 | 13 configs × 2 plataformas | médio | podar na Etapa A |
| 7 | Windows 10 mínimo | médio | decisão de produto |
| 8 | `#pragma warning` inválidos no v145 | baixo | mecânico |
| 9 | Stack overflow em x64 | baixo | `/STACK` |
| 10 | Sem testes automatizados | alto | harness de cena fixa + screenshot |

---

## 9. Estimativas

| Etapa | Escopo | Esforço |
|---|---|---|
| A.1 | Git, VS2026, poda de configs | 3–5 dias |
| A.2 | Migrar 6 projetos para v145 | 1–2 semanas |
| A.3 | Corrigir C++ (auto_ptr, shuffle, asm nop, warnings) | 1 semana |
| A.4 | Recompilar dependências Win32 | 2–4 semanas |
| A.5 | Iterar até rodar | 1–2 semanas |
| **A** | | **4–8 semanas** |
| B.1 | Adicionar x64 aos 6 projetos + solução | 1–2 semanas |
| B.2 | Assembly e anti-tamper | 1–2 semanas |
| B.3 | Truncamento de ponteiro (C4311/C4312/C4267) | 3–5 semanas |
| B.4 | Dependências x64 | 2–4 semanas |
| B.5 | Serialização, save/load, rede | 2–3 semanas |
| B.6 | Estabilização | 2–4 semanas |
| **B** | | **8–16 semanas** |

Com 2 devs, A.4 e B.4 paralelizam com o resto: ~2–4 meses no total.

---

## 10. Primeiros dez passos

1. `git init` + commit. Tag `vs2010-win32`.
2. Instalar VS2026 + Windows SDK, mantendo o VS2010.
3. Confirmar build e execução no VS2010.
4. Podar as 13 configurações para 5.
5. Criar branch `vs2026-win32`.
6. Trocar `PlatformToolset` para `v145` nos 6 projetos.
7. Remover as configs `Xbox 360`.
8. `/std:c++17` e corrigir `auto_ptr` + `random_shuffle`.
9. Recompilar Bullet e SimulWeather com v145 `/MT`.
10. Iterar até `Retail|Win32` rodar; comparar com o build VS2010.

Só depois disso começar a Etapa B. Não antecipe o x64 para "ganhar tempo" — é exatamente
o que transforma dois problemas solucionáveis em um problema ambíguo.

---

## 11. Relação com o plano Vulkan

Depois da Etapa B você tem:

- toolchain atual (C++17, STL moderna, debugger moderno);
- 64 bits, o que resolve o maior risco do plano Vulkan (pressão de memória em 32 bits);
- D3D9 funcionando em x64 — ou seja, o backend de referência para comparação por imagem;
- validação e ferramentas modernas disponíveis.

O `MIGRACAO_DX9_VULKAN.md` continua válido na íntegra. A única coisa que muda é que a
Fase 0 dele (baseline + harness) já vai estar pronta, e a estimativa de 12–18 meses pode
cair, porque parte do risco de memória e de toolchain desaparece.

Se o objetivo for **só** modernizar, pare ao fim da Etapa B: o jogo fica em D3D9, mas
compilável, debugável e com 64 bits. Se o objetivo for Vulkan, a Etapa B é o melhor
investimento possível antes dele.
