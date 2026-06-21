import { useCallback } from 'react';
import { useAppState, type AppPage } from '../AppState';

export function useSafePageNavigation() {
  return useSafeNavigation().requestPageChange;
}

export function useSafeNavigation() {
  const { state, dispatch } = useAppState();

  const confirmDiscard = useCallback(() => {
    if (state.editorSaveStatus === 'saving') return false;
    if (
      ['dirty', 'error'].includes(state.editorSaveStatus) &&
      !window.confirm('Discard unsaved chapter changes and leave this page?')
    ) {
      return false;
    }
    if (['dirty', 'error'].includes(state.editorSaveStatus)) {
      if (state.activeEditorFileId) {
        dispatch({ type: 'REVERT_EDITOR_BUFFER', fileId: state.activeEditorFileId });
      } else {
        dispatch({ type: 'SET_EDITOR_SAVE_STATUS', status: 'idle' });
      }
    }
    return true;
  }, [dispatch, state.activeEditorFileId, state.editorSaveStatus]);

  const requestPageChange = useCallback(
    (page: AppPage) => {
      if (page === state.currentPage) return true;
      if (!confirmDiscard()) return false;
      dispatch({ type: 'SET_PAGE', page });
      return true;
    },
    [confirmDiscard, dispatch, state.currentPage],
  );

  const requestWorldChange = useCallback(
    (worldId: string | null) => {
      if (worldId === state.worldId) return true;
      if (!confirmDiscard()) return false;
      dispatch({ type: 'SET_WORLD', worldId });
      return true;
    },
    [confirmDiscard, dispatch, state.worldId],
  );

  return { requestPageChange, requestWorldChange };
}
