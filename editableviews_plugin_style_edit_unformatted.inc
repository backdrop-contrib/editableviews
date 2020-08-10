<?php

/**
 * Plugin class for the Editable Unformatted style.
 */
class editableviews_plugin_style_edit_unformatted extends views_plugin_style_default {

  /**
   * Initialize a style plugin.
   */
  public function init(&$view, &$display, $options = NULL) {
    parent::init($view, $display, $options);

    // Get the helper object. This abstracts out a number of things we do here,
    // in order that other style plugins can use them too.
    $this->helper = new editableviews_style_helper($this);
  }

  public function option_definition() {
    $options = parent::option_definition();

    // Todo: this should technically be on the helper, but it's faffy as it
    // is apparently not always there, if Views code with the same
    // pattern is anything to go by!
    $options['relationship_creation_bundle'] = array('default' => array());
    $options['save_messages'] = array('default' => 'individual');

    return $options;
  }

  /**
   * The options form for the given style.
   */
  public function options_form(&$form, &$form_state) {
    parent::options_form($form, $form_state);

    // Everything we add to the options form is common and thus in the helper.
    $this->helper->options_form($form, $form_state);
  }

  public function validate() {
    $errors = parent::validate();

    $relationship_handlers = $this->display->handler->get_handlers('relationship');
    $field_handlers_grouped = $this->helper->get_editable_field_handlers_grouped();

    foreach ($relationship_handlers as $relationship_id => $relationship_handler) {
      // We don't care about required relationships.
      if ($relationship_handler->options['required']) {
        continue;
      }

      // We don't care if there are no editable fields on the relationship.
      if (empty($field_handlers_grouped[$relationship_id])) {
        continue;
      }

      if (!isset($relationship_handler->definition['editableviews_direction'])) {
        $errors[] = t("The relationship '@relationship' is not compatible with editable fields that may cause creation of entities for empty data. The relationship should be set to be 'required'.", array(
          '@relationship' => $relationship_handler->options['label'],
        ));
      }

      if (!isset($this->options['relationship_creation_bundle'][$relationship_id])) {
        $errors[] = t("Display @display is set to use a editable fields and the '@relationship' relationship is not set to be required: the bundle of entities to create on this relationship must be set in the Editable Table options.", array(
          '@display' => $this->display->display_title,
          '@relationship' => $relationship_handler->options['label'],
        ));
      }
    }

    return $errors;
  }

  /**
   * Add anything to the query that we might need to.
   */
  public function query() {
    parent::query();

    // Everything we do to the query is common and thus in the helper.
    $this->helper->query();
  }

  /**
   * Helper to get the handlers for editable fields.
   *
   * @return
   *   An array of field handlers, in the same format as get_handlers() returns,
   *   but containing only those handlers which are for editable fields.
   */
  public function get_edit_field_handlers() {
    $handlers = $this->display->handler->get_handlers('field');

    $edit_field_handlers = array();
    foreach ($handlers as $key => $handler) {
      if (!empty($handler->editable)) {
        $edit_field_handlers[$key] = $handler;
      }
    }

    return $edit_field_handlers;
  }

  /**
   * Render all of the fields for a given style and store them on the object.
   *
   * @param $result
   *   The result array from $view->result
   */
  public function render_fields($result) {
    if (!$this->uses_fields()) {
      return;
    }

    if (!isset($this->rendered_fields)) {
      parent::render_fields($result);

      $this->insert_form_elements($result);
    }

    return $this->rendered_fields;
  }

  /**
   * Insert the field form elements into the rendered View fields.
   *
   * @param $result
   *   The result array from $view->result
   */
  public function insert_form_elements($result) {
    // Get our edit field handlers.
    $edit_field_handlers = $this->get_edit_field_handlers();
    if (empty($edit_field_handlers)) {
      // There's nothing to do!
      return;
    }
    $relationship_handlers = $this->display->handler->get_handlers('relationship');

    // Build an array of the field names to make editable.
    // The keys are the id keys of the Views handlers.
    // For non-field-API fields, the definition must include this property.
    $edit_fields = array();
    // Create the keys in this so they exist even for relationships with no
    // editable fields.
    $edit_field_handlers_grouped = array_fill_keys(array_keys($relationship_handlers), array());
    $edit_field_handlers_grouped[$this->view->base_table] = array();

    foreach ($edit_field_handlers as $handler_id => $handler) {
      $edit_fields[$handler_id] = $handler->field_name();

      // Build an array of handlers grouped by relationship ID.
      // This is for the form builder to only work on the handlers that are
      // relevant to the entity's relationship.
      $field_handler_relationship_id = $handler->options['relationship'];
      if ($field_handler_relationship_id == 'none') {
        $field_handler_relationship_id = $this->view->base_table;
      }

      $edit_field_handlers_grouped[$field_handler_relationship_id][$handler_id] = $handler;
    }

    // Build an array of entities that we should be working with. We load the
    // that are implicit in the view result, creating new ones if there are any
    // gaps.
    // The entity ID fields have been added to the view result by query().
    $result_entities = array();
    foreach ($result as $index => $result_row) {
      foreach ($this->helper->relationship_entity_fields as $relationship_id => $relationship_entity_data) {
        $entity_type = $relationship_entity_data['entity_type'];
        // Get the entity ID out of the result.
        $entity_id = $result_row->{$relationship_entity_data['id_field_alias']};

        // Get the entities we work with, and build an array of them.
        if (isset($entity_id)) {
          $entity = entity_load($entity_type, $entity_id);

          $result_entities[$entity_type][$entity_id] = $entity;
        }
        else {
          if (count($edit_field_handlers_grouped[$relationship_id])) {
            // If there is no entity, and the relationship has editable fields,
            // we create one (i.e., make the object without saving it). We give
            // this a fake entity id, composed of the relationship handler id
            // and the index so it's unique.
            $entity_id = $relationship_id . ':' . $index;

            $entity = $this->helper->entity_create($relationship_id);

            $result_entities[$entity_type][$entity_id] = $entity;
          }
        }

        // Build a lookup from entity type and entity to the result row index
        // and relationship. It helps to conceptualize this as giving us
        // coordinates for where the entity has fields in the view table:
        // the index gives the row, and the relationship gives the column(s).
        // This is for the form builder to be able to get to the right result
        // row and to know which handlers to get form elements from.
        $result_indexes[$entity_type][$entity_id] = array($relationship_id, $index);

        // Build a lookup array of the same coordinates, but towards the entity:
        // keys are relationship ID then index, values are entity type and entity.
        $result_indexes_reverse[$relationship_id][$index] = array($entity_type, $entity_id);
      }
    }

    // Make a combined array of coordinate lookups, both forward and reverse.
    // TODO: eventually replace everything to work with this arrays instead of
    // the two separate ones.
    $results_coordinates = array(
      'entities_to_results' => $result_indexes,
      'results_to_entities' => $result_indexes_reverse,
    );

    // Build up some lookups pertaining to field handlers, and set the editable
    // fields on new entities.
    $result_entity_ids = array();
    foreach ($result as $index => $result_row) {
      foreach ($edit_field_handlers as $handler_id => $handler) {
        // Get the entity type and entity for this field handler from the
        // relationship lookup.
        $field_handler_relationship_id = $handler->options['relationship'];
        if ($field_handler_relationship_id == 'none') {
          $field_handler_relationship_id = $this->view->base_table;
        }

        // Add the field_name for this field handler to the list on the entity
        // which keeps track of them.
        list($entity_type, $entity_id) = $result_indexes_reverse[$field_handler_relationship_id][$index];
        if (!is_numeric($entity_id)) {
          $entity = $result_entities[$entity_type][$entity_id];
          $entity->editableviews_exposed_fields[] = $handler->field_name();
        }

        // Build a lookup array from index and field handler to the entity type
        // and entity. This is to get to the right form element to include when
        // we finally render our fields.
        // Just get the entity coordinates from the relationship lookup.
        $result_entity_ids[$index][$handler_id] = $result_indexes_reverse[$field_handler_relationship_id][$index];
      }
    }

    // Now we have built up all our entities, go over them again and add
    // the connecting properties to any new ones.
    // In other words:
    //  - On a forward relationship, the existing entity on the relationship's
    //    base needs to point to the new entity that is (potentially) about to
    //    be saved.
    //  - On a reverse relationship, the new entity that is about to be created
    //    needs to point back to the existing entity on the relationship's base.
    // Here we figure out the id we need to point to, and the property to point
    // to it in.
    $this->helper->connect_new_entities($result_entities, $results_coordinates, $edit_field_handlers_grouped);

    // Load up the form render array.
    $this->get_form($result_entities, $results_coordinates, $edit_field_handlers_grouped);

    // Doctor the view's rendered fields to add in the form elements for
    // the appropriate entity and field.
    foreach ($this->rendered_fields as $index => $rendered_fields) {
      foreach ($edit_fields as $handler_id => $field_name) {
        // Get the already rendered field.
        $rendered_field = $this->rendered_fields[$index][$handler_id];

        // Get the entity type and entity that this field handler shows from our
        // lookup array, so that we can pick out the form element to render
        // for it.
        list($entity_type, $entity_id) = $result_entity_ids[$index][$handler_id];

        // TODO! theme this!!
        $this->rendered_fields[$index][$handler_id] = '';
        $rendered_div = '<div class="views-row-edit-static">' . $rendered_field . '</div>';
        if (!isset($this->view->field[$handler_id]->options['suppress_value'])) {
          $this->rendered_fields[$index][$handler_id] = $rendered_div;
        }
        else {
          if (!$this->view->field[$handler_id]->options['suppress_value']) {
            $this->rendered_fields[$index][$handler_id] = $rendered_div;
          }
        }
        $this->rendered_fields[$index][$handler_id] .= '<div class="views-row-edit-edit">' . drupal_render($this->form[$entity_type][$entity_id][$field_name]) . '</div>';
      }
    }
  }

  /**
   * Helper method. Retrieves the form render array.
   *
   * @param $entities
   *  An array of entities to get the form for, keyed first by entity type and
   *  then by entity id.
   * @param $results_coordinates
   *  An array containing coordinates lookups for getting entity type and entity
   *  from row id and relationship, and the same the other way round. Contains:
   *  - 'entities_to_results': A nested array keyed by entity type then entity
   *    id. The final values are flat arrays of the relationship id and result
   *    index.
   *  - 'results_to_entities': A nested array keyed by relationship id then
   *    result index. The final values are flat arrays of the entity type and
   *    entity id.
   * @param $edit_field_handlers
   *  An array of field handlers to provide form elements for, grouped by
   *  their relationship.
   *  See editableviews_entity_form() for details.
   */
  public function get_form($entities, $results_coordinates, $edit_field_handlers) {
    // Create a dynamic form ID using the base name (for quick recognition
    // in hook_forms()) and the view name. This allows hook_form_alter() to
    // target forms for specific views. We don't add the display name as there's
    // no clean way to mark the separation between that and the view name.
    // @see editableviews_forms()
    $form_id = 'editableviews_entity_form' . '_' . $this->view->name;

    // We store this rather than return it, as it's used in different places.
    $this->form = drupal_get_form($form_id, $entities, $results_coordinates, $edit_field_handlers, $this->view);
  }

  /**
   * Render the display in this style.
   */
  public function render() {
    // Get the rendered view output.
    $view_render = parent::render();

    // Stick it INSIDE the form as plain markup, so that the HTML FORM element
    // goes around everything.
    $this->form['view'] = array(
      '#markup' => $view_render,
    );

    return $this->form;
  }

}
