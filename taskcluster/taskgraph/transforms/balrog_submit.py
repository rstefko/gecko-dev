# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.
"""
Transform the per-locale balrog task into an actual task description.
"""

from __future__ import absolute_import, print_function, unicode_literals

from taskgraph.loader.single_dep import schema
from taskgraph.transforms.base import TransformSequence
from taskgraph.util.attributes import copy_attributes_from_dependent_job
from taskgraph.util.scriptworker import (
    get_balrog_server_scope, get_worker_type_for_scope
)
from taskgraph.transforms.task import task_description_schema
from voluptuous import Any, Required, Optional


# Voluptuous uses marker objects as dictionary *keys*, but they are not
# comparable, so we cast all of the keys back to regular strings
task_description_schema = {str(k): v for k, v in task_description_schema.schema.iteritems()}

# shortcut for a string where task references are allowed
taskref_or_string = Any(
    basestring,
    {Required('task-reference'): basestring})

balrog_description_schema = schema.extend({
    # unique label to describe this balrog task, defaults to balrog-{dep.label}
    Optional('label'): basestring,

    # treeherder is allowed here to override any defaults we use for beetmover.  See
    # taskcluster/taskgraph/transforms/task.py for the schema details, and the
    # below transforms for defaults of various values.
    Optional('treeherder'): task_description_schema['treeherder'],

    Optional('attributes'): task_description_schema['attributes'],

    # Shipping product / phase
    Optional('shipping-product'): task_description_schema['shipping-product'],
    Optional('shipping-phase'): task_description_schema['shipping-phase'],
})


transforms = TransformSequence()
transforms.add_validate(balrog_description_schema)


@transforms.add
def make_task_description(config, jobs):
    for job in jobs:
        dep_job = job['primary-dependency']

        treeherder = job.get('treeherder', {})
        treeherder.setdefault('symbol', 'c-Up(N)')
        dep_th_platform = dep_job.task.get('extra', {}).get(
            'treeherder', {}).get('machine', {}).get('platform', '')
        treeherder.setdefault('platform',
                              "{}/opt".format(dep_th_platform))
        treeherder.setdefault(
            'tier',
            dep_job.task.get('extra', {}).get('treeherder', {}).get('tier', 1)
        )
        treeherder.setdefault('kind', 'build')

        attributes = copy_attributes_from_dependent_job(dep_job)

        treeherder_job_symbol = dep_job.attributes.get('locale', 'N')

        if dep_job.attributes.get('locale'):
            treeherder['symbol'] = 'c-Up({})'.format(treeherder_job_symbol)
            attributes['locale'] = dep_job.attributes.get('locale')

        label = job['label']

        description = (
            "Balrog submission for locale '{locale}' for build '"
            "{build_platform}/{build_type}'".format(
                locale=attributes.get('locale', 'en-US'),
                build_platform=attributes.get('build_platform'),
                build_type=attributes.get('build_type')
            )
        )

        upstream_artifacts = [{
            "taskId": {"task-reference": "<beetmover>"},
            "taskType": "beetmover",
            "paths": [
                "public/manifest.json"
            ],
        }]

        server_scope = get_balrog_server_scope(config)

        task = {
            'label': label,
            'description': description,
            'worker-type': get_worker_type_for_scope(config, server_scope),
            'worker': {
                'implementation': 'balrog',
                'upstream-artifacts': upstream_artifacts,
                'balrog-action': 'submit-locale',
            },
            'dependencies': {'beetmover': dep_job.label},
            'attributes': attributes,
            'run-on-projects': dep_job.attributes.get('run_on_projects'),
            'treeherder': treeherder,
            'shipping-phase': job.get('shipping-phase', 'promote'),
            'shipping-product': job.get('shipping-product'),
        }

        yield task
