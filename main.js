import { createGame } from './game.js';

const SURVIVE_MOVES = 10;
const MOVETIME = 1000;

const survivedEl = document.getElementById('survived');
const statusEl = document.getElementById('status');

for (const el of document.querySelectorAll('#goal, .goal')) el.textContent = String(SURVIVE_MOVES);

document.getElementById('ok').addEventListener('click', () => {
  document.getElementById('popup').remove();
});

const messages = {
  loading: 'Loading Blitz',
  turn: 'Your move, you are White',
  thinking: 'Blitz is thinking',
  win: 'You beat Blitz',
  draw: 'Draw',
  loss: 'Checkmate. Try again',
};

let unlocked = false;

function unlock() {
  if (unlocked) return;
  unlocked = true;
  document.body.classList.add('unlocked');
}

function survived(state) {
  if (state.status === 'turn' && state.moves >= SURVIVE_MOVES) return true;
  if (state.status === 'over' && state.result !== 'loss') return true;
  return false;
}

createGame(document.getElementById('board'), {
  movetime: MOVETIME,
  onUpdate(state) {
    survivedEl.textContent = String(Math.min(state.moves, SURVIVE_MOVES));
    statusEl.textContent = state.status === 'over' ? messages[state.result] : messages[state.status];
    if (survived(state)) unlock();
  },
});
