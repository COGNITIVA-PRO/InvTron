# InvTron — carga do Firebase + tela de detalhe do item

## O que foi gerado a partir da planilha

A partir de `InvTron.xlsx` (433 itens), foram criadas 5 tabelas:

|Tabela|Conteúdo|Chave|
|-|-|-|
|`classes`|6 valores únicos da coluna Classe|slug do nome|
|`categorias`|34 valores únicos da coluna Categoria|slug do nome|
|`subcategorias`|93 subcategorias, aninhadas por categoria: `/subcategorias/{idCategoria}/{idSubcategoria}`|slug do nome|
|`itens`|todas as colunas da planilha, com `classeId`, `categoriaId`, `subcategoriaId` apontando para as tabelas acima|Part No. (saneado)|
|`estoques`|`{ saldo: 1 }` para cada item|mesma chave do item|

Também gerei uma 6ª tabela, **`itens\\\_por\\\_subcategoria`**, que não foi pedida explicitamente mas é necessária para o firmware conseguir listar "quais itens pertencem a esta subcategoria" sem precisar baixar os 433 itens de uma vez — ela é só um índice `{idSubcategoria: {idItem: descricao}}`.

## Sobre a coluna "Foto"

As imagens não podem ir dentro do Realtime Database (não é feito para blobs binários). O que fiz:

1. Extraí as 433 imagens vinculadas às células da coluna Foto (usando os metadados internos do xlsx que ligam cada linha à sua imagem).
2. Redimensionei cada uma para **160×160**, convertendo para JPEG — o pacote todo caiu de 50 MB para **\~2,3 MB**, viável para o ESP32 baixar por Wi-Fi.
3. Nomeei cada arquivo com a chave do item, ex.: `IPEX-K.jpg`.
4. No banco, o campo `foto` de cada item guarda **só o nome do arquivo** (ex. `"IPEX-K.jpg"`), não uma URL completa.

⚠️ Reparei que a planilha vincula a mesma imagem a vários itens semelhantes em algumas categorias (ex. resistores de valores diferentes usando a mesma foto genérica). Não tentei "corrigir" isso — extraí exatamente o que estava vinculado a cada linha na planilha.

## Arquivos neste pacote

* **`invtron\\\_firebase\\\_import.json`** — as 6 tabelas juntas, para importar de uma vez.
* `invtron\\\_classes.json`, `invtron\\\_categorias.json`, `invtron\\\_subcategorias.json`, `invtron\\\_itens.json`, `invtron\\\_estoques.json`, `invtron\\\_itens\\\_por\\\_subcategoria.json` — cada tabela separada, caso prefira importar/atualizar uma de cada vez.
* **`fotos.zip`** — as 433 fotos já redimensionadas, prontas para hospedar.
* **`InvTron.ino`** — firmware atualizado.

## Passo 1 — Importar os dados no Firebase

No console do Firebase → Realtime Database → menu (⋮) → **Import JSON**. Isso **substitui** o conteúdo do nó selecionado — na primeira carga, importe `invtron\\\_firebase\\\_import.json` na raiz do banco.

## Passo 2 — Hospedar as fotos

O RTDB só guarda o *nome* do arquivo; alguém precisa servir o `.jpg` via HTTP. A opção mais simples dentro do próprio ecossistema Firebase é o **Firebase Hosting**:

1. Extraia `fotos.zip` numa pasta, ex. `public/fotos/`.
2. Instale o Firebase CLI (`npm install -g firebase-tools`), rode `firebase login`, depois `firebase init hosting` no projeto (aponte o diretório público para a pasta que contém `fotos/`).
3. `firebase deploy --only hosting`.
4. Isso te dá uma URL fixa, algo como `https://SEU-PROJETO.web.app/fotos/IPEX-K.jpg`.

Qualquer outro host estático (Firebase Storage com regras públicas, GitHub Pages, etc.) também funciona — o firmware só precisa de uma URL base que, concatenada ao nome do arquivo, resulte num link direto para o `.jpg`.

## Passo 3 — Ajustar o firmware

Em `InvTron.ino`, troque:

```cpp
#define FOTO\\\_BASE\\\_URL "https://SEU-PROJETO.web.app/fotos/"
```

pela URL real de onde as fotos ficaram hospedadas (**termine com barra `/`**).

## O que mudou no `InvTron.ino`

* **Fluxo de navegação** atualizado para o novo esquema: Categoria → Subcategoria → Item → **tela de detalhe** (a antiga etapa "Local" foi removida, já que o novo modelo de dados não usa mais local por item — só estoque agregado).
* **Nova tela de detalhe do item** (estado `AJUSTE\\\_ESTOQUE`, reaproveitando o mesmo estado de antes):

  * **Foto** do item no canto superior esquerdo (baixada via HTTP na hora, decodificada e desenhada com `tft.drawJpg`).
  * **Descrição** do item no canto superior direito.
  * **Saldo em estoque** abaixo da descrição, alterado girando o encoder e **confirmado ao pressionar** — grava em `/estoques/{idItem}/saldo`.
  * Se a foto não carregar (sem link, erro de rede, arquivo grande demais), é desenhado um quadro cinza com "sem foto" no lugar, sem travar a tela.
* Adicionado timeout de 20s na autenticação do Firebase, para não travar silenciosamente se as credenciais estiverem erradas (mesmo ajuste que fizemos antes).

## Observações importantes

* O buffer de download da foto tem um limite de 40 KB — bem folgado para os thumbnails de 160×160 gerados (a maioria fica na casa de 5–15 KB), mas está lá para não estourar a RAM do ESP32 se algum arquivo maior for hospedado por engano.
* Como antes, o barramento do display está em 8 MHz — não mexi nisso.
* Não modifiquei a lógica do encoder nem a estrutura de autenticação/Wi-Fi, só o fluxo de menus e a tela final.

