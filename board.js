const FILES = 'abcdefgh';
const DRAG_THRESHOLD = 3;

export function createBoard(root, chess, options = {}) {
  const onMove = options.onMove || (() => {});
  const squares = new Map();
  let selected = null;
  let lastMove = null;
  let enabled = true;
  let drag = null;
  let promotion = null;

  root.classList.add('board');

  for (let rank = 8; rank >= 1; rank--) {
    for (let file = 0; file < 8; file++) {
      const name = FILES[file] + rank;
      const el = document.createElement('div');
      el.className = 'square ' + ((file + rank) % 2 === 0 ? 'dark' : 'light');
      el.dataset.square = name;

      if (file === 0) {
        const coord = document.createElement('span');
        coord.className = 'coord rank';
        coord.textContent = String(rank);
        el.appendChild(coord);
      }
      if (rank === 1) {
        const coord = document.createElement('span');
        coord.className = 'coord file';
        coord.textContent = FILES[file];
        el.appendChild(coord);
      }

      root.appendChild(el);
      squares.set(name, el);
    }
  }

  function pieceKey(piece) {
    return piece.color + piece.type.toUpperCase();
  }

  function pieceUrl(key) {
    return `url(pieces/${key}.svg)`;
  }

  function kingSquare(color) {
    for (const [name] of squares) {
      const piece = chess.get(name);
      if (piece && piece.type === 'k' && piece.color === color) return name;
    }
    return null;
  }

  function legalMoves(from) {
    return chess.moves({ square: from, verbose: true });
  }

  function render() {
    const turn = chess.turn();
    const checked = chess.inCheck() ? kingSquare(turn) : null;

    for (const [name, el] of squares) {
      const piece = chess.get(name);
      let pieceEl = el.querySelector('.piece');
      if (piece) {
        if (!pieceEl) {
          pieceEl = document.createElement('div');
          pieceEl.className = 'piece';
          el.appendChild(pieceEl);
        }
        const key = pieceKey(piece);
        if (pieceEl.dataset.piece !== key) {
          pieceEl.dataset.piece = key;
          pieceEl.style.backgroundImage = pieceUrl(key);
        }
        pieceEl.classList.toggle('hidden', !!drag && drag.from === name);
      } else if (pieceEl) {
        pieceEl.remove();
      }

      el.classList.toggle('selected', selected === name);
      el.classList.toggle('last', !!lastMove && (lastMove.from === name || lastMove.to === name));
      el.classList.toggle('check', checked === name);
      el.classList.toggle('movable', enabled && !!piece && piece.color === turn);
      el.classList.remove('hint', 'capture');
    }

    if (selected) {
      for (const move of legalMoves(selected)) {
        const el = squares.get(move.to);
        el.classList.add('hint');
        if (move.captured) el.classList.add('capture');
      }
    }
  }

  function squareAt(clientX, clientY) {
    const rect = root.getBoundingClientRect();
    const x = Math.floor(((clientX - rect.left) / rect.width) * 8);
    const y = Math.floor(((clientY - rect.top) / rect.height) * 8);
    if (x < 0 || x > 7 || y < 0 || y > 7) return null;
    return FILES[x] + (8 - y);
  }

  function applyMove(from, to, promo) {
    const move = chess.move({ from, to, promotion: promo });
    selected = null;
    lastMove = { from: move.from, to: move.to };
    render();
    onMove(move);
  }

  function tryMove(from, to) {
    const candidates = legalMoves(from).filter((m) => m.to === to);
    if (candidates.length === 0) return false;
    if (candidates[0].promotion) {
      openPromotion(from, to, candidates[0].color);
    } else {
      applyMove(from, to);
    }
    return true;
  }

  function openPromotion(from, to, color) {
    closePromotion();
    selected = from;
    render();

    const file = FILES.indexOf(to[0]);
    const up = color === 'b';
    const panel = document.createElement('div');
    panel.className = 'promotion' + (up ? ' up' : '');
    panel.style.left = `${file * 12.5}%`;
    if (up) panel.style.bottom = '0';
    else panel.style.top = '0';

    for (const type of ['q', 'n', 'r', 'b']) {
      const choice = document.createElement('div');
      choice.className = 'choice';
      choice.style.backgroundImage = pieceUrl(color + type.toUpperCase());
      choice.addEventListener('pointerdown', (event) => {
        event.stopPropagation();
        closePromotion();
        applyMove(from, to, type);
      });
      panel.appendChild(choice);
    }

    const close = document.createElement('div');
    close.className = 'close';
    close.textContent = '✕';
    close.addEventListener('pointerdown', (event) => {
      event.stopPropagation();
      closePromotion();
      selected = null;
      render();
    });
    panel.appendChild(close);

    root.appendChild(panel);
    promotion = panel;
  }

  function closePromotion() {
    if (promotion) {
      promotion.remove();
      promotion = null;
    }
  }

  function startDrag(from, key, event) {
    const rect = root.getBoundingClientRect();
    const ghost = document.createElement('div');
    ghost.className = 'drag';
    ghost.style.backgroundImage = pieceUrl(key);
    root.appendChild(ghost);
    drag = { from, ghost, startX: event.clientX, startY: event.clientY, moved: false, size: rect.width / 8 };
    positionGhost(event.clientX, event.clientY);
  }

  function positionGhost(clientX, clientY) {
    const rect = root.getBoundingClientRect();
    const half = drag.size / 2;
    drag.ghost.style.transform = `translate(${clientX - rect.left - half}px, ${clientY - rect.top - half}px)`;
  }

  function endDrag() {
    if (!drag) return;
    drag.ghost.remove();
    drag = null;
    root.classList.remove('dragging');
    for (const el of squares.values()) el.classList.remove('hover');
  }

  function onPointerDown(event) {
    if (!enabled || event.button !== 0) return;
    if (promotion) {
      closePromotion();
      selected = null;
      render();
      return;
    }

    const name = squareAt(event.clientX, event.clientY);
    if (!name) return;
    const piece = chess.get(name);
    const own = piece && piece.color === chess.turn();

    if (selected && selected !== name && tryMove(selected, name)) return;

    if (own) {
      const reselect = selected === name;
      selected = name;
      root.setPointerCapture(event.pointerId);
      startDrag(name, pieceKey(piece), event);
      drag.reselect = reselect;
      render();
      return;
    }

    selected = null;
    render();
  }

  function onPointerMove(event) {
    if (!drag) return;
    if (!drag.moved) {
      const dx = event.clientX - drag.startX;
      const dy = event.clientY - drag.startY;
      if (Math.hypot(dx, dy) < DRAG_THRESHOLD) return;
      drag.moved = true;
      root.classList.add('dragging');
      render();
    }
    positionGhost(event.clientX, event.clientY);
    const over = squareAt(event.clientX, event.clientY);
    for (const [name, el] of squares) el.classList.toggle('hover', name === over);
  }

  function onPointerUp(event) {
    if (!drag) return;
    const from = drag.from;
    const moved = drag.moved;
    const reselect = drag.reselect;
    const target = squareAt(event.clientX, event.clientY);
    endDrag();

    if (!moved) {
      if (reselect) selected = null;
      render();
      return;
    }

    if (target && target !== from && tryMove(from, target)) return;

    if (target !== from) selected = null;
    render();
  }

  root.addEventListener('pointerdown', onPointerDown);
  root.addEventListener('pointermove', onPointerMove);
  root.addEventListener('pointerup', onPointerUp);
  root.addEventListener('pointercancel', () => { endDrag(); render(); });

  render();

  return {
    render,
    setEnabled(value) {
      enabled = value;
      if (!enabled) {
        selected = null;
        closePromotion();
        endDrag();
      }
      render();
    },
    setLastMove(move) {
      lastMove = move ? { from: move.from, to: move.to } : null;
      render();
    },
    reset() {
      selected = null;
      lastMove = null;
      closePromotion();
      endDrag();
      render();
    },
  };
}
