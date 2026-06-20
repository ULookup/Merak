import { useCallback } from 'react';
import { useAppState, type AppPage } from '../AppState';

export function useSafePageNavigation() {
  const { state, dispatch } = useAppState();

  return useCallback(
    (page: AppPage) => {
      if (page === state.currentPage) return true;
      if (state.editorSaveStatus === 'saving') return false;
      if (
        ['dirty', 'error'].includes(state.editorSaveStatus) &&
        !window.confirm('Discard unsaved chapter changes and leave this page?')
      ) {
        return false;
      }
      if (['dirty', 'error'].includes(state.editorSaveStatus)) {
        dispatch({ type: 'SET_EDITOR_SAVE_STATUS', status: 'idle' });
      }
      dispatch({ type: 'SET_PAGE', page });
      return true;
    },
    [dispatch, state.currentPage, state.editorSaveStatus],
  );
}
