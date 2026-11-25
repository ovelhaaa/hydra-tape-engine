export function createTransportController({ player, transportState, setStatus = () => {} }) {
  if (!player) {
    throw new Error('createTransportController requires a player element');
  }
  if (!transportState) {
    throw new Error('createTransportController requires a transportState');
  }

  const handleTimelineEnded = async () => {
    if (!transportState.getState().isRepeatEnabled) return;

    player.currentTime = 0;
    try {
      await player.play();
      setStatus('Repeat ON: restarting from the beginning.');
    } catch (error) {
      setStatus(`Repeat failed to restart playback: ${error.message}`);
    }
  };

  player.addEventListener('ended', handleTimelineEnded);

  return {
    handleTimelineEnded,
    dispose() {
      player.removeEventListener('ended', handleTimelineEnded);
    }
  };
}
