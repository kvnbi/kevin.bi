import { createGame } from './game.js';

const SURVIVE_MOVES = 10;

const survivedEl = document.getElementById('survived');
const statusEl = document.getElementById('status');

for (const el of document.querySelectorAll('#goal, .goal')) el.textContent = String(SURVIVE_MOVES);

const messages = {
  loading: 'Loading Blitz',
  turn: 'Your move, you are White',
  thinking: 'Blitz is thinking',
  win: 'You beat Blitz',
  draw: 'Draw',
  loss: 'Checkmate. Try again',
};

createGame(document.getElementById('board'), {
  onUpdate(state) {
    survivedEl.textContent = String(Math.min(state.moves, SURVIVE_MOVES));
    statusEl.textContent = state.status === 'over' ? messages[state.result] : messages[state.status];
  },
});
