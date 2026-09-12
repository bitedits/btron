# Vector Extensions for LLM Agents and BTRON Users

The vector layer turns a BTRON volume into a **native semantic store** while remaining 100 % compatible with the classic Real-Body / TAD model. Documents, images, code, and even UI state keep their normal records; embeddings become additional typed records that any program (or LLM agent) can create, update, and query.

## 1. Core idea in BTRON terms

A Real Body can now contain:

| Record type | Meaning |
|-------------|---------|
| `RT_TADDATA` / `RT_TEXT` / `RT_IMAGE` … | The human-visible content (unchanged) |
| `RT_VECTOR` (new) | One or more embedding vectors that describe the content |
| Optional index records | Volume-level ANN structures that point back to FIDs |

Because everything lives inside the same Real Body graph, the usual BTRON operations (open, link, snapshot, delete) automatically keep the vectors consistent.

## 2. What an `RT_VECTOR` record looks like

```c
#define RT_VECTOR  17

typedef struct {
    UH   dim;          /* e.g. 384, 768, 1024, 1536 */
    UH   metric;       /* 0 = cosine, 1 = L2, 2 = inner-product */
    UH   quant;        /* 0 = f32, 1 = f16, 2 = int8, 3 = binary */
    UH   flags;        /* chunked, multi-vector, etc. */
    UW   nvec;         /* number of vectors in this record */
    UW   model_id;     /* optional: which embedding model produced them */
    /* followed by nvec × dim values (quantized according to quant) */
} VectorHeader;
```

A single document can therefore carry:

- one vector for the whole document,
- one vector per paragraph / section,
- separate vectors for embedded images or code blocks,
- different model versions side-by-side (so you can re-embed later).

## 3. Practical scenarios for LLM agents

**Semantic retrieval over the whole desktop**

```c
// “Find the 8 most similar documents to this paragraph”
float query[768] = { … };          // embedding of the current context
VectorHit hits[8];
vol_vector_search(vol, query, 768, 8, hits);

for (int i = 0; i < 8; i++) {
    // hits[i].fid  → open the Real Body
    // hits[i].rec  → the exact RT_TADDATA / RT_TEXT record
    // hits[i].score
}
```

The agent never has to build or maintain an external vector database; the volume *is* the database.

**Chunk-level RAG**

When a long TAD document is saved, the editor (or a background service) automatically:

1. Splits the text into overlapping chunks.
2. Embeds each chunk.
3. Stores the vectors as multiple entries inside one `RT_VECTOR` record (or as sibling records).
4. Updates the volume-level ANN index.

Later an agent can ask “which paragraphs are most relevant to this question?” and receive precise record offsets.

**Multi-modal search**

An image Real Body can carry its own visual embedding. A text query embedding can be compared against both text and image vectors (cross-modal search) because they live in the same index with a declared metric.

**Agent memory & workspace**

Each agent session can be a Real Body that accumulates:

- conversation turns (`RT_TADDATA`),
- tool results,
- intermediate reasoning,
- embeddings of the whole session.

Snapshots (CoW) give the agent free “time-travel” and the ability to branch alternative plans without duplicating data.

**Personal knowledge base**

A user (or an agent acting for the user) can drop any document, e-mail, PDF-extracted text, or screenshot into a volume. Background embedding keeps the semantic index fresh. Later the same agent can answer “what did I write about topic X last year?” by pure similarity search.

## 4. Benefits for ordinary BTRON desktop users

Even without any LLM the vector layer is useful:

- **Smarter “find similar”** in the file manager – right-click a document → “Show similar files”.
- **Better fusen / sticker suggestions** – the system can propose related documents or templates.
- **Content-aware search** that works across languages (because embeddings are language-agnostic).
- **Automatic tagging / clustering** of large collections without manual metadata.

Because the vectors are ordinary records, classic BTRON tools continue to work; they simply ignore the `RT_VECTOR` records they do not understand.

## 5. Volume-level index (optional but powerful)

When the superblock feature flag `VECTOR` is set, the volume maintains a secondary structure (HNSW, IVF-Flat, or a simple flat index for small volumes). The index stores:

```
(vector_id) → (FID, record_index, offset_inside_record)
```

Updates are transactional with the rest of the filesystem (journal or CoW), so the index never goes out of sync with the Real Bodies.

For very large volumes the index itself can be sharded or stored as a special Real Body that the volume manager treats specially.

## 6. Embedding pipeline (how vectors get created)

Typical flow:

1. User or agent writes / imports content → normal Real Body + TAD records.
2. A lightweight service (or the editor itself) detects the change.
3. It calls an embedding model (local GGUF, remote API, or an on-device NPU).
4. It writes one or more `RT_VECTOR` records into the same Real Body.
5. It notifies the volume to update the ANN index.

Because the model identifier is stored inside the vector header, you can later re-embed everything with a newer model without losing the old vectors.

## 7. API surface (minimal additions)

```c
/* Create / update vectors for an open file */
ER fil_set_vectors(ID fd, const VectorHeader *hdr, const void *data);

/* Search */
typedef struct {
    FID  fid;
    W    rec_idx;
    UW   vec_id;
    float score;
} VectorHit;

ER vol_vector_search(Volume *v,
                     const float *query, UW dim,
                     UW k, VectorHit *out);

/* Convenience: embed + search in one call (if the volume has a default model) */
ER vol_semantic_search(Volume *v, const char *text, UW k, VectorHit *out);
```

Everything else (open, read, link, snapshot, delete) stays exactly the same.

## 8. Why this is powerful for both worlds

- **LLM agents** get a persistent, versioned, multi-modal memory that is just “files”.
- **Classic BTRON users** get semantic search and smarter organisation without leaving the familiar Real-Body desktop.
- **Developers** keep a single coherent model: everything is still a Real Body containing typed records.
- **Future models** can be swapped in by writing new `RT_VECTOR` records; old ones remain readable.

In short, the vector extension turns every BTRON volume into a lightweight, crash-safe, snapshot-capable vector database whose primary key is the same FID that the rest of the system already understands.

# Credits

Namdak Tonpa and Grok 4.5
