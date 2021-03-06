// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_BROWSER_INDEXED_DB_LEVELDB_LEVELDB_TRANSACTION_H_
#define CONTENT_BROWSER_INDEXED_DB_LEVELDB_LEVELDB_TRANSACTION_H_

#include <set>
#include <string>
#include <vector>

#include "base/memory/ref_counted.h"
#include "base/memory/scoped_ptr.h"
#include "content/browser/indexed_db/leveldb/avltree.h"
#include "content/browser/indexed_db/leveldb/leveldb_comparator.h"
#include "content/browser/indexed_db/leveldb/leveldb_database.h"
#include "content/browser/indexed_db/leveldb/leveldb_iterator.h"
#include "content/browser/indexed_db/leveldb/leveldb_slice.h"

namespace content {

class LevelDBWriteBatch;

class CONTENT_EXPORT LevelDBTransaction
    : public base::RefCounted<LevelDBTransaction> {
 public:
  static scoped_refptr<LevelDBTransaction> Create(LevelDBDatabase* db);

  void Put(const LevelDBSlice& key, std::vector<char>* value);
  void Remove(const LevelDBSlice& key);
  bool Get(const LevelDBSlice& key, std::string* value, bool* found);
  bool Commit();
  void Rollback();

  scoped_ptr<LevelDBIterator> CreateIterator();

 private:
  explicit LevelDBTransaction(LevelDBDatabase* db);
  virtual ~LevelDBTransaction();
  friend class base::RefCounted<LevelDBTransaction>;

  struct AVLTreeNode {
    AVLTreeNode();
    ~AVLTreeNode();
    std::vector<char> key;
    std::vector<char> value;
    bool deleted;

    AVLTreeNode* less;
    AVLTreeNode* greater;
    int balance_factor;
    DISALLOW_COPY_AND_ASSIGN(AVLTreeNode);
  };

  struct AVLTreeAbstractor {
    typedef AVLTreeNode* handle;
    typedef size_t size;
    typedef LevelDBSlice key;

    handle GetLess(handle h) { return h->less; }
    void SetLess(handle h, handle less) { h->less = less; }
    handle GetGreater(handle h) { return h->greater; }
    void SetGreater(handle h, handle greater) { h->greater = greater; }

    int GetBalanceFactor(handle h) { return h->balance_factor; }
    void SetBalanceFactor(handle h, int bf) { h->balance_factor = bf; }

    int CompareKeyKey(const key& ka, const key& kb) {
      return comparator_->Compare(ka, kb);
    }
    int CompareKeyNode(const key& k, handle h) {
      return CompareKeyKey(k, key(h->key));
    }
    int CompareNodeNode(handle ha, handle hb) {
      return CompareKeyKey(key(ha->key), key(hb->key));
    }

    static handle Null() { return 0; }

    const LevelDBComparator* comparator_;
  };

  typedef AVLTree<AVLTreeAbstractor> TreeType;

  class TreeIterator : public LevelDBIterator {
   public:
    static scoped_ptr<TreeIterator> Create(LevelDBTransaction* transaction);
    virtual ~TreeIterator();

    virtual bool IsValid() const OVERRIDE;
    virtual void SeekToLast() OVERRIDE;
    virtual void Seek(const LevelDBSlice& slice) OVERRIDE;
    virtual void Next() OVERRIDE;
    virtual void Prev() OVERRIDE;
    virtual LevelDBSlice Key() const OVERRIDE;
    virtual LevelDBSlice Value() const OVERRIDE;
    bool IsDeleted() const;
    void Reset();

   private:
    explicit TreeIterator(LevelDBTransaction* transaction);
    mutable TreeType::Iterator iterator_;  // Dereferencing this is non-const.
    TreeType* tree_;
    LevelDBTransaction* transaction_;
    std::vector<char> key_;
  };

  class TransactionIterator : public LevelDBIterator {
   public:
    virtual ~TransactionIterator();
    static scoped_ptr<TransactionIterator> Create(
        scoped_refptr<LevelDBTransaction> transaction);

    virtual bool IsValid() const OVERRIDE;
    virtual void SeekToLast() OVERRIDE;
    virtual void Seek(const LevelDBSlice& target) OVERRIDE;
    virtual void Next() OVERRIDE;
    virtual void Prev() OVERRIDE;
    virtual LevelDBSlice Key() const OVERRIDE;
    virtual LevelDBSlice Value() const OVERRIDE;
    void TreeChanged();

   private:
    explicit TransactionIterator(scoped_refptr<LevelDBTransaction> transaction);
    void HandleConflictsAndDeletes();
    void SetCurrentIteratorToSmallestKey();
    void SetCurrentIteratorToLargestKey();
    void RefreshTreeIterator() const;
    bool TreeIteratorIsLower() const;
    bool TreeIteratorIsHigher() const;

    scoped_refptr<LevelDBTransaction> transaction_;
    const LevelDBComparator* comparator_;
    mutable scoped_ptr<TreeIterator> tree_iterator_;
    scoped_ptr<LevelDBIterator> db_iterator_;
    LevelDBIterator* current_;

    enum Direction {
      FORWARD,
      REVERSE
    };
    Direction direction_;
    mutable bool tree_changed_;
  };

  void Set(const LevelDBSlice& key,
           std::vector<char>* value,
           bool deleted);
  void ClearTree();
  void RegisterIterator(TransactionIterator* iterator);
  void UnregisterIterator(TransactionIterator* iterator);
  void NotifyIteratorsOfTreeChange();

  LevelDBDatabase* db_;
  const LevelDBSnapshot snapshot_;
  const LevelDBComparator* comparator_;
  TreeType tree_;
  bool finished_;
  std::set<TransactionIterator*> iterators_;
};

class LevelDBWriteOnlyTransaction {
 public:
  static scoped_ptr<LevelDBWriteOnlyTransaction> Create(LevelDBDatabase* db);

  ~LevelDBWriteOnlyTransaction();
  void Remove(const LevelDBSlice& key);
  bool Commit();

 private:
  explicit LevelDBWriteOnlyTransaction(LevelDBDatabase* db);

  LevelDBDatabase* db_;
  scoped_ptr<LevelDBWriteBatch> write_batch_;
  bool finished_;
};

}  // namespace content

#endif  // CONTENT_BROWSER_INDEXED_DB_LEVELDB_LEVELDB_TRANSACTION_H_
