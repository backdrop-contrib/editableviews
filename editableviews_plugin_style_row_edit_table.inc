<?php

/**
 * Plugin class for the Editable Table style.
 */
class editableviews_plugin_style_row_edit_table extends views_plugin_style_table {

  /**
   * Initialize a style plugin.
   */
  function init(&$view, &$display, $options = NULL) {
    parent::init($view, $display, $options);

    // Get the helper object. This abstracts out a number of things we do here,
    // in order that other style plugins can use them too.
    $this->helper = new editableviews_style_helper($this);
  }

  function option_definition() {
    $options = parent::option_definition();

    // Todo: this should technically be on the helper, but it's faffy as it
    // is apparently not always there, if Views code with the same
    // pattern is anything to go by!
    $options['relationship_creation_bundle'] = array('default' => array());

    return $options;
  }

  /**
   * The options form for the given style.
   */
  function options_form(&$form, &$form_state) {
    parent::options_form($form, $form_state);

    // Everything we add to the options form is common and thus in the helper.
    $this->helper->options_form($form, $form_state);
  }

  function validate() {
    $errors = parent::validate();

    $relationship_handlers = $this->display->handler->get_handlers('relationship');
    $field_handlers_grouped = $this->helper->get_editable_field_handlers_grouped();
    //dsm($field_handlers_grouped);

    foreach ($relationship_handlers as $relationship_id => $relationship_handler) {
      // We don't care about required relationships.
      if ($relationship_handler->options['required']) {
        continue;
      }

      //dsm($relationship_handler);

      // We don't care if there are no editable fields on the relationship.
      if (empty($field_handlers_grouped[$relationship_id])) {
        continue;
      }

      if (!isset($relationship_handler->definition['editableviews_direction'])) {
        $errors[] = t("The relationship '@relationship' is not compatible with editable fields that may cause creation of entities for empty data. The relationship should be set to be 'required'.", array(
          '@relationship' => $relationship_handler->options['label']
        ));
      }

      if (!isset($this->options['relationship_creation_bundle'][$relationship_id])) {
        $errors[] = t("Display @display is set to use a editable fields and the '@relationship' relationship is not set to be required: the bundle of entities to create on this relationship must be set in the Editable Table options.", array(
          '@display' => $this->display->display_title,
          '@relationship' => $relationship_handler->options['label']
        ));
      }
    }

    return $errors;
  }

  /**
   * Helper to get the handlers for editable fields.
   *
   * @return
   *  An array of field handlers, in the same format as get_handlers() returns,
   *  but containing only those handlers which are for editable fields.
   */
  function get_edit_field_handlers() {
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
  function render_fields($result) {
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
  function insert_form_elements($result) {
    // Get our edit field handlers.
    $edit_field_handlers = $this->get_edit_field_handlers();
    //dsm($edit_field_handlers, '$edit_field_handlers');
    $relationship_handlers = $this->display->handler->get_handlers('relationship');

    //dsm($this->view->relationship);

    // Build an array of the field names to make editable.
    // The keys are the id keys of the Views handlers.
    // For non-field-API fields, the definition must include this property.
    $edit_fields = array();
    foreach ($edit_field_handlers as $handler_id => $handler) {
      //dsm($handler, "field handler $handler_id");
      $edit_fields[$handler_id] = $handler->definition['field_name'];
    }

    // Build an array of entities that we should be working with.
    // It is up to each handler to implement get_entity_type() and get_entity(),
    // and thus tell us the entity it is working on for the current row.
    // This is because with relationships, each handler could be working on a
    // different entity.
    $result_entities = array();
    foreach ($result as $index => $result) {
      foreach ($edit_field_handlers as $handler_id => $handler) {
        // We ask each field handler to tell us the entity type it works on, and
        // to give us its entity for the current result row.
        // This will lead to duplication for fields that are on the same
        // relationship. Ideally, the entity would be obtained from each
        // relationship (and the view base of course).
        $entity_type = $handler->get_entity_type();
        $entity = $handler->get_entity($result);

        if (isset($entity)) {
          list($entity_id, ) = entity_extract_ids($entity_type, $entity);
        }
        else {
          // If there is no entity, we create one (i.e., make the object without
          // saving it). We give this a fake entity id, composed of the
          // relationship handler alias and the index so it's unique.
          $entity_id = $handler->relationship . ':' . $index;

          // Only create a new entity if we haven't already for this
          // relationship and index.
          if (!isset($result_indexes_reverse[$handler->relationship][$index])) {
            $entity = $this->helper->entity_create($handler);
          }
          else {
            // We've already made this one. Pick it up so we can work on it.
            $entity = $result_entities[$entity_type][$entity_id];
          }

          // Add the field_name for this field handler to the list on the entity
          // which keeps track of them.
          $entity->editableviews_exposed_fields[] = $handler->definition['field_name'];
        }

        // We need to remember a bunch of different things to work with later...

        // Build an array of the entities, nested first by entity type
        // and then by entity ID, as we could have multiple entities of
        // the same or different types on the same result row.
        $result_entities[$entity_type][$entity_id] = $entity;

        // Build a lookup from entity type and entity to the result row index
        // and relationship. It helps to conceptualize this as giving us
        // coordinates for where the entity has fields in the view table:
        // the index gives the row, and the relationship gives the column(s).
        // This is for the form builder to be able to get to the right result
        // row and to know which handlers to get form elements from.
        $relationship = isset($handler->relationship) ? $handler->relationship : 'base';
        $result_indexes[$entity_type][$entity_id] = array($relationship, $index);

        // Build a lookup array of the same coordinates, but towards the entity:
        // keys are index then relationship, values are entity type and entity.
        $result_indexes_reverse[$relationship][$index] = array($entity_type, $entity_id);

        // Build a lookup array from index and field handler to the entity type
        // and entity. This is to get to the right form element to include when
        // we finally render our fields.
        $result_entity_ids[$index][$handler_id] = array($entity_type, $entity_id);

        // Build an array of handlers grouped by relationship.
        // This is for the form builder to only work on the handlers that are
        // relevant to the entity's relationship.
        $edit_field_handlers_grouped[$relationship][$handler_id] = $handler;
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
    $this->helper->connect_new_entities($result_entities, $result_indexes, $result_indexes_reverse);

    //dsm($result_entities, '$result_entities');

    // Load up the form render array.
    $this->get_form($result_entities, $result_indexes, $edit_field_handlers_grouped);

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
        $this->rendered_fields[$index][$handler_id] = '<div class="views-row-edit-static">' . $rendered_field . '</div>';
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
   * @param $result_indexes
   *  A lookup from entity type and id to row id and relationship.
   *  See editableviews_entity_form() for details.
   * @param $edit_field_handlers
   *  An array of field handlers to provide form elements for, grouped by
   *  their relationship.
   *  See editableviews_entity_form() for details.
   *
   * @return
   *  The form render array from drupal_get_form().
   */
  function get_form($entities, $result_indexes, $edit_field_handlers) {
    // Create a dynamic form ID using the base name (for quick recognition
    // in hook_forms()) and the view name. This allows hook_form_alter() to
    // target forms for specific views. We don't add the display name as there's
    // no clean way to mark the separation between that and the view name.
    // @see editableviews_forms()
    $form_id = 'editableviews_entity_form' . '_' . $this->view->name;

    // We store this rather than return it, as it's used in different places.
    $this->form = drupal_get_form($form_id, $entities, $result_indexes, $edit_field_handlers, $this->view->name, $this->view->current_display);
  }

  /**
   * Render the display in this style.
   */
  function render() {
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

/**
 * Helper class for the style plugin.
 *
 * This abstracts out a number of things the style plugin needs to do, in order
 * that other style plugins can use them too.
 */
class editableviews_style_helper {

  function __construct(&$plugin) {
    $this->plugin = &$plugin;
  }

  /**
   * Provide common options for editable style plugins.
   */
  function options_form(&$form, &$form_state) {
    // Add a fieldset to allow setting of a creation bundle for all the
    // relationships that are non-required. This is because a non-required
    // relationship may cause empty data in the result, and if this has editable
    // fields, then entering data into those field's form elements causes the
    // creation of a new entity. Which we need a bundle for.
    $relationship_handlers = $this->plugin->display->handler->get_handlers('relationship');
    // Get our edit field handlers.
    $edit_field_handlers = $this->plugin->get_edit_field_handlers();

    // Collect the relationships these are on.
    $edit_relationship_handlers = array();
    foreach ($edit_field_handlers as $field_handler_id => $field_handler) {
      // Because we're not in the process of querying, the relationship is only
      // set in the options.
      $relationship_id = $field_handler->options['relationship'];

      // Skip edit field handlers that are on the base.
      if ($relationship_id == 'none') {
        continue;
      }

      // Argh, do we need to contend with the alias of the relationship here??

      // Skip a relationship that is required: these will never provide an empty
      // row, and so never require entity creation.
      if ($relationship_handlers[$relationship_id]->options['required']) {
        continue;
      }

      // If we're still here, this is a relationship we need to consider.
      $edit_relationship_handlers[$relationship_id] = $relationship_handlers[$relationship_id];
    }

    $form['relationship_creation_bundle'] = array(
      '#type' => 'fieldset',
      '#title' => t('Relationship entity creation bundles'),
      '#description' => t('A relationship which is set to not required may produce empty form elements, which thus require the creation of a new entity if populated with data. The bundle for these new entities must be specified here.'),
      '#weight' => -1,
    );

    foreach ($edit_relationship_handlers as $relationship_id => $relationship_handler) {
      // The 'base' of a relationship is the table it brings.
      $table = $relationship_handler->definition['base'];
      // Get the entity type from the table.
      $table_data = views_fetch_data($table);
      $entity_type = $table_data['table']['entity type'];
      $entity_info = entity_get_info($entity_type);

      $options = array();
      foreach ($entity_info['bundles'] as $bundle => $bundle_info) {
        $options[$bundle] = $bundle_info['label'];
      }

      $form['relationship_creation_bundle'][$relationship_id] = array(
        '#type' => 'select',
        '#title' => t('Bundle for new entities created on %label', array(
          '%label' => $relationship_handler->options['label'],
        )),
        '#description' => t('Select the %entity entity bundle for entities created on this relationship.', array(
          '%entity' => $entity_info['label'],
        )),
        '#options' => $options,
        '#required' => TRUE,
      );
      // We have to check the default value, as the key in the array is variable
      // because it's the relationship handler ID. That means that Views won't
      // have it set in option_definition().
      if (isset($this->plugin->options['relationship_creation_bundle'][$relationship_id])) {
        $form['relationship_creation_bundle'][$relationship_id]['#default_value'] = $this->plugin->options['relationship_creation_bundle'][$relationship_id];
      }
    }
  }

  /**
   * Returns a new (unsaved) entity for the given field handler.
   *
   * This is needed when the field handler is on a non-required relationship,
   * and a particular result row has no data there. We create a new entity for
   * FieldAPI to work on, and potentially save it on submission if the user
   * enters data.
   *
   * @param $field_handler
   *  An editable field handler from the view.
   *
   * @return
   *  A new, unsaved entity. The entity type is implied by the handler, and
   *  should be known by the caller. The bundle will be set on this, given by
   *  the style plugin's options.
   */
  function entity_create($field_handler) {
    $entity_type = $field_handler->get_entity_type();
    // This is complex. We know the entity type, but we need to be told
    // the bundle: that's one for the plugin settings.
    // Then when it's created, we need to know how to set the relationship
    // field.
    $entity_info = entity_get_info($entity_type);
    // Assume this exists, as it must do if the entity is fieldable, and
    // if your entity is not fieldable, what are you doing here? ;)
    $bundle_key = $entity_info['entity keys']['bundle'];

    $relationship_id = $this->get_relationship_id_from_alias($field_handler->relationship);

    $values = array(
      // The bundle of the new entity is set in the options for this
      // style plugin. This has to be set by the user, because there is
      // absolutely no other way to sniff this out!
      // TODO: cloud cuckoo land, but a form element field to specify
      // the bundle for each row would be nice!
      $bundle_key => $this->plugin->options['relationship_creation_bundle'][$relationship_id],
    );

    // Just a little bit of sugar to save this having to be done in a custom
    // form submit handler: for nodes and comments, set the uid property
    // to the current user. We would do this with anything that has a uid
    // property, but entity_get_property_info() calls it 'author' and it's just
    // starting to get faffy now.
    if ($entity_type == 'node' || $entity_type == 'comment') {
      $values['uid'] = $GLOBALS['user']->uid;
    }

    $entity = entity_create($entity_type, $values);

    // Add our own property to the entity, where we keep track of the properties
    // that are exposed as form elements in the view. This is how we will
    // determine whether or not to save it when the form is submitted.
    $entity->editableviews_exposed_fields = array();

    // Add our own property to specify whether this needs to be saved or not.
    // @see editableviews_entity_form_submit_build_values()
    $entity->editableviews_needs_save = FALSE;

    return $entity;
  }

  /**
   * Sets the properties so that new entities connect to existing ones.
   *
   * For a forward relationship, the existing entity must know it has to point
   * to the new entity once it has been saved.
   * For a reverse relationship, the new entity must have the right property set
   * (e.g. an entityreference field) so that it point back to the existing
   * entity.
   *
   * @param $result_entities
   *  The array of result entities from the style plugin. Passed by reference
   *  so the entities can be altered. (TODO: is this actually needed??)
   * @param $result_indexes
   *  The forward index of entity ids.
   * @param $result_indexes_reverse
   *  The reverse index of entity ids.
   */
  function connect_new_entities(&$result_entities, $result_indexes, $result_indexes_reverse) {
    $relationship_handlers = $this->plugin->display->handler->get_handlers('relationship');

    foreach (array_keys($result_entities) as $entity_type) {
      foreach ($result_entities[$entity_type] as $entity_id => $entity) {
        // New entities have a non-numeric fake id we just gave them.
        if (!is_numeric($entity_id)) {
          // Get the views coordinates for this entity.
          list($relationship, $index) = $result_indexes[$entity_type][$entity_id];
          // Get the relationship handler for the relationship alias.
          $relationship_id = $this->get_relationship_id_from_alias($relationship);
          $relationship_handler = $relationship_handlers[$relationship_id];
          //dsm($relationship_handler);

          if (!isset($relationship_handler->definition['editableviews_direction'])) {
            // Well this is a problem.
            // TODO: validation on the view for this!
            throw new Exception(t('Entity creation is not supported on the relationship %label', array(
              '%label' => $relationship_handler->options['label'],
            )));
          }

          // Get the relationship that the relationship is on, so we can then
          // get the entity for that relationship.
          if (isset($relationship_handler->relationship)) {
            $relationship_relationship = $relationship_handler->relationship;
          }
          else {
            $relationship_relationship = 'base';
          }

          if ($relationship_handler->definition['editableviews_direction'] == 'forward') {
            // Get the entity on our relationship\s reletaionship -- same
            // as for reverse.
            // Get the entity out of the imaginary Views grid that is on the same
            // row as us, and in the $relationship_relationship relationship...
            list($referring_entity_type, $referring_entity_id) = $result_indexes_reverse[$relationship_relationship][$index];
            $referring_entity = $result_entities[$referring_entity_type][$referring_entity_id];

            // Store this entity's details on the current, new entity, so that
            // when (and if!) we save it, we can go and make the referring
            // entity point to it.
            $entity->editableviews_future_reference = array(
              'entity_type' => $referring_entity_type,
              'entity_id' => $referring_entity_id,
              'field_name' => $relationship_handler->definition['field_name'],
            );
          }
          else {
            // Would be nice to factor this out to a helper method, say
            // '$this->new_entity_set_reverse_connection()' but we'd need to
            // pass so many variables it's probably just as faffy.

            // Get the entity out of the imaginary Views grid that is on the same
            // row as us, and in the $relationship_relationship relationship...
            list($referred_entity_type, $referred_entity_id) = $result_indexes_reverse[$relationship_relationship][$index];
            $referred_entity = $result_entities[$referred_entity_type][$referred_entity_id];

            // From here on, this is just reverse relationships!
            // Forward relationships are TODO!
            $wrapper = entity_metadata_wrapper($entity_type, $entity);

            // This is what we need to set on the new entity in a reverse relationship.
            $relationship_field_name = $relationship_handler->definition['field_name'];
            // Make the new entity point to the entity on its relationship's
            // relationship.
            $wrapper->{$relationship_field_name}->set($referred_entity_id);
          }
        }
      }
    }
  }

  /**
   * Get the relationship ID for a relationship alias.
   *
   * This is needed because $field_handler->relationship is an alias, but
   * the relationship handlers array is keyed by id, and the only way AFAICT
   * to get from one to the other is to faffily iterate.
   *
   * @TODO: file a patch on Views for this to be made easier!
   * see http://drupal.org/node/1941976
   */
  function get_relationship_id_from_alias($relationship_alias) {
    $relationship_handlers = $this->plugin->display->handler->get_handlers('relationship');
    foreach ($relationship_handlers as $relationship_id => $relationship_handler) {
      // TODO! have to account for BASE!!!!!
      // except is this ever called for a base???
      if ($relationship_handler->alias == $relationship_alias) {
        break;
      }
    }
    return $relationship_id;
  }

  /**
   * Get editable field handlers grouped by relationship id.
   */
  function get_editable_field_handlers_grouped() {
    $editable_field_handlers = $this->plugin->get_edit_field_handlers();

    $editable_field_handlers_grouped = array();
    foreach ($editable_field_handlers as $field_handler_id => $field_handler) {
      //dsm($field_handler, '$field_handler');
      $relationship_id = $field_handler->options['relationship'];
      if ($relationship_id == 'none') {
        // TODO: tidy up this WTF!
        $relationship_id = 'base';
      }

      $editable_field_handlers_grouped[$relationship_id][$field_handler_id] = $field_handler;
    }

    return $editable_field_handlers_grouped;
  }

}
