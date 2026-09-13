import { Chess } from './vendor/chess.js/chess.js';
import { createBoard } from './board.js';

export function createGame(root, options = {}) {
  const movetime = options.movetime || 300;
  const resetDelay = options.resetDelay ?? 2500;
  const startFen = options.fen || null;
  const onUpdate = options.onUpdate || (() => {});

  const chess = startFen ? new Chess(startFen) : new Chess();
  const board = createBoard(root, chess, { onMove: onPlayerMove });
  const worker = new Worker('engine/worker.js');

  const state = { status: 'loading', result: null, moves: 0 };
  let resetTimer = null;

  board.setEnabled(false);

  function send(command) {
    worker.postMessage(command);
  }

  function update(status, result) {
    state.status = status;
    state.result = result || null;
    state.moves = Math.ceil(chess.history().length / 2);
    onUpdate({ ...state });
  }

  function positionCommand() {
    const moves = chess.history({ verbose: true }).map((m) => m.from + m.to + (m.promotion || ''));
    const base = startFen ? `position fen ${startFen}` : 'position startpos';
    return moves.length ? `${base} moves ${moves.join(' ')}` : base;
  }

  function outcome() {
    if (chess.isCheckmate()) return chess.turn() === 'w' ? 'loss' : 'win';
    if (chess.isDraw() || chess.isStalemate()) return 'draw';
    return null;
  }

  function finish(result) {
    board.setEnabled(false);
    update('over', result);
    if (result === 'loss' && resetDelay !== null) {
      resetTimer = setTimeout(reset, resetDelay);
    }
  }

  function onPlayerMove() {
    const result = outcome();
    if (result) return finish(result);
    board.setEnabled(false);
    update('thinking');
    send(positionCommand());
    send(`go movetime ${movetime}`);
  }

  function onBestMove(line) {
    const uci = line.split(/\s+/)[1];
    if (!uci || uci === '(none)') return finish(outcome() || 'draw');
    const move = chess.move({ from: uci.slice(0, 2), to: uci.slice(2, 4), promotion: uci[4] });
    board.setLastMove(move);
    const result = outcome();
    if (result) return finish(result);
    board.setEnabled(true);
    update('turn');
  }

  function reset() {
    clearTimeout(resetTimer);
    resetTimer = null;
    if (startFen) chess.load(startFen);
    else chess.reset();
    board.reset();
    board.setEnabled(true);
    send('ucinewgame');
    update('turn');
  }

  worker.onmessage = (event) => {
    const line = event.data;
    if (line === 'ready') {
      send('uci');
      send('ucinewgame');
      board.setEnabled(true);
      update('turn');
    } else if (line.startsWith('bestmove')) {
      onBestMove(line);
    }
  };

  return {
    chess,
    board,
    reset,
    get state() {
      return { ...state };
    },
  };
}
