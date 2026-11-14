# Render nova do MCLA para Android

A revisão usa os projetos públicos listados abaixo. Cada coluna separa o que
foi documentado de uma decisão para o MCLA. Os projetos de Xbox original e
PS2 entram apenas como referências de compatibilidade e operação.

| Projeto | Backend e apresentação | Texturas e formatos | EDRAM e resolves | Shaders | Problemas, uso e limites |
| --- | --- | --- | --- | --- | --- |
| [MCLA ForkBR](https://github.com/contatg/mcla-recomp-forkbr) | `LoadGpuPlugin` inicializa `xenos`; ReXApp liga o presenter à janela SDL3 | O diagnóstico da v0.1.52 registra fetch inválido e fallbacks YUV 4:2:2 | O título tem regras de readback por endereço e formato | Tradução SPIR-V em runtime e cache Vulkan persistente | Ruído verde observado no SD680 e espera do guest na transição. Manter o plugin antigo selecionável e medir apresentação e simulação separadamente. |
| [Zandatsu](https://github.com/dfdx047/Zandatsu) | Renut intercepta Direct3D do MGR e usa Vulkan via plume. Xenia emula a GPU como outra opção. Perfis, seleção de drivers e upscale na saída | Renut trabalha com os recursos D3D do jogo. Os patches Xenia são outro caminho | Os draws do Renut conhecem as passagens do MGR. A emulação Xenia cobre EDRAM e resolves | Extração/tradução do título no nativo; tradutor e cache do backend emulado | Separar seleção do renderer da configuração do driver. O mínimo publicado do perfil nativo cita Adreno 619; desempenho e correções do MGR precisam de validação no MCLA/610. |
| [Dragon-Blast](https://github.com/dfdx047/Dragon-Blast) | Projeto Android/ReXGlue; o [guia do Zandatsu](https://github.com/dfdx047/Zandatsu/blob/main/docs/BUILDING.md) identifica a série de patches herdada do Dragon Fury | O README não especifica pipeline de formatos | Sem evidência suficiente sobre resolves específicos | ReXGlue como base | Referência de integração Android; a identidade do SDK não estabelece comportamento de vídeo ou correção de texturas. |
| [mgr-nx-deko](https://github.com/tuancapoo/mgr-nx-deko) | Hooks D3D do MGR, Renut e backend deko3d do plume no Switch. Resolução interna configurável | Recursos tratados pelos hooks D3D do título | Organização dos passes é específica do MGR | Extrai contêineres de shader do próprio jogo e compila no primeiro uso | Aproveitar separação entre extração, compilação e execução. Os hooks e a API de apresentação do Switch são específicos da plataforma e do jogo. |
| [reNut](https://github.com/masterspike52/reNut) | ReXGlue para CPU e chamadas D3D do Banjo transformadas em operações plume | Interface D3D interceptada, recursos do título | Tradução por chamadas do jogo; não documenta um command processor PM4 genérico | XenosRecomp com shaders pré-compilados no executável | Priorizar capturas e casos de teste de compatibilidade. O conjunto de hooks do Banjo não cobre outros jogos. |
| [ReXGlue](https://github.com/rexglue/rexglue-sdk) | No SDK fixado e adaptado pelo fork, `IGraphicsSystem` separa provider/presenter, GPU do guest e encerramento | O backend Xenos existente gerencia texturas guest | Interfaces expõem ring buffer, callback de interrupção e tamanho EDRAM | O plugin Xenos implementa a tradução e o armazenamento de shaders | Reutilizar ABI e acesso a AdrenoTools. O plugin F1 faz seus próprios draws e usa o presenter Vulkan compartilhado para a janela. |
| [Xenia](https://github.com/xenia-project/xenia) | O backend Vulkan divide command processor, cache de texturas, cache de alvos e presenter | A chave da textura precisa representar formato, endereço, layout, pitch e conversão | Diferenciar propriedade dos tiles e destino do resolve | Tradutor SPIR-V representa as semânticas Xenos | Útil como referência de comportamento para testes. Validar explicitamente sincronização e features antes de selecionar caminhos do backend móvel. |
| [XenDroid](https://github.com/rfandango/XenDroid) | Xenia em Android com seleção de driver Vulkan | As correções dependem da versão do driver e do título | A base é o caminho emulado de Xenia | Tradução do emulador | O projeto recomenda reproduzir no backend Vulkan do Xenia Edge antes de classificar defeitos Android. Útil para separar bug de plataforma e bug Xenos. |
| [Xendroid-Plus](https://github.com/phforner0/Xendroid-Plus) | Vulkan com Turnip, melhorias de descritores e capturas para validar correções | Documenta fast paths de decode de sinal e upload direto | Elimina transferências de EDRAM que um draw sobrescreve | Compilação assíncrona e cache persistente | Medir cada fast path com teste de imagem. Upload que depende do formato ou driver precisa de uma alternativa geral. |
| [ARMSX2](https://github.com/ARMSX2/ARMSX2) | Emulador PS2 com backends e drivers móveis. O Zandatsu credita a base de bugs, proteção de cache e ADPF | Formatos de GS não servem de referência para Xenos | Memória de GS é outra arquitetura | Cache/organização são ideias transferíveis | Registrar driver real, validar cabeçalho do cache e separar caches por identidade do driver. |
| [XenosRecomp](https://github.com/hedge-dev/XenosRecomp) | Ferramenta de shader, sem apresentação | Lê descrições e metadados dos contêineres Xbox 360 | Não cobre EDRAM ou resolves | Xenos → HLSL → DXIL ou SPIR-V via DXC | O projeto ressalva o teste de controle de fluxo complexo. Testar por shader e oferecer tradução em runtime para misses. |
| [plume](https://github.com/renderbag/plume) | RHI sobre Vulkan, D3D12 e Metal | Abstrai imagens, buffers e formatos do host | Não implementa a semântica do EDRAM Xenos | Consome shaders para a API do host | Útil para separar recursos da GPU, tradução do guest e apresentação; a RHI não substitui PM4 nem conversão Xenos. |
| [NFSMW Android](https://github.com/codepdbh/nfsmw-android) | Render nativo Vulkan 1.1 com requisitos de descriptor indexing; opção experimental GLES | Recursos do título; não assumir layout de mídia igual ao MCLA | Implementação nativa do título; sem equivalência genérica PM4 | Pacote gerado localmente e vinculado à edição do jogo | Vincular cache a título/versão e reservar alternativa quando faltar feature. Vulkan 1.1 sozinho não satisfaz o caminho publicado. |
| [Black Android](https://github.com/RanggaCasper/Black-Android) | Xbox original x86/D3D8 convertido para Vulkan, janela SDL3 | Oferece escala de FMV e filtro de textura | Sem EDRAM Xenos | Cache de shaders por app | A documentação declara Vulkan 1.1 mesmo sem dynamic rendering e timeline semaphores. Preferir framebuffer/render pass e fence compatíveis na F1. |
| [ShaolinMonks Android](https://github.com/RanggaCasper/ShaolinMonks-Android) | Xbox original/XboxXBE-Recomp com Vulkan e SDL3 | Oferece anisotropia e ajuste de resolução | Sem EDRAM Xenos | Cache de shaders por app | Requisitos documentados incluem Vulkan 1.1. Controles/escala no launcher são ideias independentes da GPU emulada. |

## Integração e ordem dos testes

O loader Android resolve `librexgpu-<nome>.so` no namespace do APK e exige
`rex_gpu_create` e `rex_gpu_abi_version`. O plugin pode fornecer
`SetupPresentation`, `SetupGuestGpu`, `provider` e `presenter`.
A F1 valida somente o plugin de saída Vulkan com padrões próprios.
A F2 precisa implementar as obrigações do ring buffer e sincronização do
guest antes que o teste de progressão seja interpretável. F3 e F4 exigem
casos de teste por formato, shader e resolve. A execução do guest, o
shaderpack do jogo e a medição de FPS na cidade vêm nessas fases.

O controle da tela e dos drivers do Zandatsu é referência conceitual.
O código de teste deste fork foi escrito para o MCLA. Os READMEs e créditos
acima são evidência sobre o comportamento publicado, sem validar a execução
desses projetos no Snapdragon 680.

## Teste da F1 no Android

Com **Extras → Render: Nova**, toque em **Testar render nova**. Esta fase abre
a janela de teste mesmo sem dados do jogo. Envie um print com as quatro
regiões visíveis: barras RGB/branco, gradiente cinza, triângulo amarelo sobre
azul e textura quadriculada. Se a tela ficar preta, verde ou incompleta,
exporte também o diagnóstico e inclua as linhas `[build]`, `[driver]`,
`[pipe]`, `[tex]` e `[present]`. **Antiga** abre o jogo com os dados já
importados e com o cache que ele já usa. A F1 não inicializa o guest e seus
FPS medem somente a apresentação do padrão.

## F2 — simulação com rasterização desligada

A v0.1.55 mostrou todas as regiões do padrão no Snapdragon 680, com 27,26 FPS
no print e 440 apresentações registradas antes da pausa. Esses números
medem somente a F1.

A Nova agora inicia o XEX pelo botão **Play**. Seu frontend reutiliza a
interpretação PM4, os registradores, as interrupções e o write-back do
ponteiro de leitura do ReXGlue. O backend de diagnóstico descarta
microcódigo, draws e resolves. Um worker publica o quadro azul quando
recebe swaps, com um único pedido pendente para evitar criar uma fila
quando a apresentação atrasa. A opção Antiga usa a fábrica e o cache
Xenos existentes. O padrão F1 segue disponível em Extras.

No teste F2, use a mesma sequência de controles que usava para chegar à
cidade com a Antiga. Como a saída é azul, confirme o avanço pelos sons e
pelas linhas `[stage]` e `[present]`. Envie o diagnóstico e anote até que
ponto conseguiu avançar. Se a simulação continuar lenta, `[hostcall]` e
`[guestwait2]` ajudam a separar espera por trabalho do guest de espera por
dados ausentes que um resolve ou memexport normalmente produziria.
